// Isolated GPU feasibility benchmark, never injected into the game. Synthetic
// eligible-point queries approximate a possible nav broadphase, NOT native BFS,
// polygon projection, physics, visibility or complete bot logic.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
#include "json.hpp"
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
static void Check(HRESULT hr) { if(FAILED(hr))throw std::runtime_error("DirectX HRESULT "+std::to_string(uint32_t(hr))); }
static double Ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Point {float x,y,z,group;};
struct Result {float distance=std::numeric_limits<float>::max();uint32_t index=UINT32_MAX;};
static_assert(sizeof(Point)==16 && sizeof(Result)==8);
static void Consider(const Point& q,const Point& p,uint32_t index,Result& best) {
    if(q.group!=p.group)return;
    const float x=q.x-p.x,y=q.y-p.y,z=q.z-p.z;
    const float d=(x*x+y*y)+z*z;
    if(d<best.distance || (d==best.distance && index<best.index))best={d,index};
}
class Tree {
    struct Node {uint32_t point;int left=-1,right=-1,axis=0;};
    const std::vector<Point>& points;
    std::vector<uint32_t> order;
    std::vector<Node> nodes;
    static float Axis(const Point& p,int axis){return axis==0?p.x:axis==1?p.y:p.z;}
    int Build(size_t lo,size_t hi,int depth) {
        if(lo==hi)return -1;
        const auto mid=(lo+hi)/2;const int axis=depth%3;
        std::nth_element(order.begin()+lo,order.begin()+mid,order.begin()+hi,[&](auto a,auto b){return Axis(points[a],axis)<Axis(points[b],axis);});
        const int at=int(nodes.size());nodes.push_back({order[mid],-1,-1,axis});
        const int left=Build(lo,mid,depth+1),right=Build(mid+1,hi,depth+1);
        nodes[at].left=left;nodes[at].right=right;return at;
    }
    void Search(int at,const Point& query,Result& best)const {
        if(at<0)return;
        const auto& node=nodes[at];const auto& p=points[node.point];
        Consider(query,p,node.point,best);
        const float delta=Axis(query,node.axis)-Axis(p,node.axis);
        Search(delta<0?node.left:node.right,query,best);
        if(delta*delta<=best.distance)Search(delta<0?node.right:node.left,query,best);
    }
public:
    explicit Tree(const std::vector<Point>& p):points(p),order(p.size()){
        std::iota(order.begin(),order.end(),0);nodes.reserve(p.size());Build(0,p.size(),0);
    }
    Result Query(const Point& p)const {Result result;Search(0,p,result);return result;}
};
static const char* shader=R"(
struct Point {float3 position;float category;};
struct Result {float distance;uint index;};
StructuredBuffer<Point> points:register(t0);
StructuredBuffer<Point> queries:register(t1);
RWStructuredBuffer<Result> results:register(u0);
cbuffer Counts:register(b0) {uint pointCount;uint queryCount;};
groupshared float distances[64];groupshared uint indices[64];
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint lane:SV_GroupIndex) {
    if(group.x>=queryCount)return;
    Point q=queries[group.x];float best=3.402823466e+38F;uint found=0xffffffff;
    for(uint i=lane;i<pointCount;i+=64) {
        Point p=points[i];
        if(p.category==q.category) {
            precise float3 delta=q.position-p.position;
            precise float d=(delta.x*delta.x+delta.y*delta.y)+delta.z*delta.z;
            if(d<best || (d==best && i<found)){best=d;found=i;}
        }
    }
    distances[lane]=best;indices[lane]=found;GroupMemoryBarrierWithGroupSync();
    for(uint step=32;step>0;step/=2) {
        if(lane<step) {
            float d=distances[lane+step];uint index=indices[lane+step];
            if(d<distances[lane] || (d==distances[lane] && index<indices[lane])) {
                distances[lane]=d;indices[lane]=index;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane==0){Result r;r.distance=distances[0];r.index=indices[0];results[group.x]=r;}
}
)";
class Compute {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12QueryHeap> timestamps;
    ComPtr<ID3D12Resource> points,pointUpload,requests,requestUpload,output,readback,timeReadback;
    HANDLE event=nullptr;uint64_t serial=0,frequency=0;
    uint32_t pointCount=0;
    static constexpr unsigned maxQueries=200;
    ComPtr<ID3D12Resource> Buffer(size_t bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,bool uav=false) {
        D3D12_HEAP_PROPERTIES props{};props.Type=heap;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
        desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)));
        return result;
    }
    void Transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands->ResourceBarrier(1,&barrier);
    }
    void Reset(){Check(allocator->Reset());Check(commands->Reset(allocator.Get(),pipeline.Get()));}
    void Submit(){Check(commands->Close());ID3D12CommandList* list=commands.Get();queue->ExecuteCommandLists(1,&list);Check(queue->Signal(fence.Get(),++serial));}
    void Wait(){
        if(fence->GetCompletedValue()<serial){Check(fence->SetEventOnCompletion(serial,event));
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU fence timeout");}
        Check(device->GetDeviceRemovedReason());
    }
    static void Upload(ID3D12Resource* resource,const void* data,size_t bytes){
        void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(resource->Map(0,&empty,&mapped));
        memcpy(mapped,data,bytes);D3D12_RANGE written{0,bytes};resource->Unmap(0,&written);
    }
public:
    std::string adapterName;uint64_t videoMemory=0;
    ComPtr<ID3D12InfoQueue> infoQueue;
    ComPtr<IDXGIAdapter3> memoryAdapter;
    explicit Compute(bool debug){
        if(debug){ComPtr<ID3D12Debug> layer;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)));layer->EnableDebugLayer();}
        ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        for(unsigned i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
            DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));
            if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))){
                char name[256]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr);
                adapterName=name;videoMemory=desc.DedicatedVideoMemory;break;
            }
            adapter.Reset();
        }
        if(!device)throw std::runtime_error("No hardware D3D12 adapter (software fallback intentionally disabled)");
        Check(adapter.As(&memoryAdapter));
        if(debug)Check(device.As(&infoQueue));
        D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
        Check(queue->GetTimestampFrequency(&frequency));
        Check(device->CreateCommandAllocator(queueDesc.Type,IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0,queueDesc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));Check(commands->Close());
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("CreateEvent failed");
        D3D12_ROOT_PARAMETER parameters[4]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[0].Descriptor.ShaderRegister=0;
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[1].Descriptor.ShaderRegister=1;
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[2].Descriptor.ShaderRegister=0;
        parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[3].Constants={0,0,2};
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=4;rootDesc.pParameters=parameters;
        ComPtr<ID3DBlob> signature,errors,bytecode;
        Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors));
        Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));
        auto hr=D3DCompile(shader,strlen(shader),"bot_candidate_benchmark",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_IEEE_STRICTNESS,0,&bytecode,&errors);
        if(FAILED(hr) && errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());Check(hr);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};pipelineDesc.pRootSignature=root.Get();pipelineDesc.CS={bytecode->GetBufferPointer(),bytecode->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
        D3D12_QUERY_HEAP_DESC heapDesc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,4,0};Check(device->CreateQueryHeap(&heapDesc,IID_PPV_ARGS(&timestamps)));
        requests=Buffer(maxQueries*sizeof(Point),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        requestUpload=Buffer(maxQueries*sizeof(Point),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        output=Buffer(maxQueries*sizeof(Result),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        readback=Buffer(maxQueries*sizeof(Result),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        timeReadback=Buffer(32,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    }
    ~Compute(){if(event)CloseHandle(event);}
    uint64_t LocalMemory() const {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        Check(memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info));
        return info.CurrentUsage;
    }
    Json Diagnostics() const {
        Json messages=Json::array();
        if(infoQueue)for(UINT64 i=0;i<infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
            SIZE_T bytes=0;Check(infoQueue->GetMessage(i,nullptr,&bytes));
            std::vector<unsigned char> buffer(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            Check(infoQueue->GetMessage(i,m,&bytes));
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)
                messages.push_back({{"severity",int(m->Severity)},{"id",int(m->ID)},{"text",m->pDescription}});
        }
        return messages;
    }
    double SetPoints(const std::vector<Point>& input){
        const auto start=Clock::now();pointCount=unsigned(input.size());
        points=Buffer(input.size()*sizeof(Point),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        pointUpload=Buffer(input.size()*sizeof(Point),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        Upload(pointUpload.Get(),input.data(),input.size()*sizeof(Point));Reset();
        commands->CopyBufferRegion(points.Get(),0,pointUpload.Get(),0,input.size()*sizeof(Point));
        Transition(points.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Submit();Wait();return Ms(start,Clock::now());
    }
    struct Timing {double total,submit,wait,kernel,upload,readback;};
    Timing Run(const std::vector<Point>& queries,std::vector<Result>& results){
        if(queries.empty() || queries.size()>maxQueries)throw std::runtime_error("Invalid batch size");
        const auto start=Clock::now();const auto bytes=queries.size()*sizeof(Point);
        Upload(requestUpload.Get(),queries.data(),bytes);Reset();
        commands->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        Transition(requests.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(requests.Get(),0,requestUpload.Get(),0,bytes);
        Transition(requests.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commands->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        commands->SetComputeRootSignature(root.Get());commands->SetComputeRootShaderResourceView(0,points->GetGPUVirtualAddress());
        commands->SetComputeRootShaderResourceView(1,requests->GetGPUVirtualAddress());commands->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());
        const uint32_t counts[]={pointCount,unsigned(queries.size())};commands->SetComputeRoot32BitConstants(3,2,counts,0);
        commands->Dispatch(unsigned(queries.size()),1,1);
        commands->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2);
        Transition(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyBufferRegion(readback.Get(),0,output.Get(),0,queries.size()*sizeof(Result));
        Transition(output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,3);
        commands->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,4,timeReadback.Get(),0);
        Submit();const auto submitted=Clock::now();Wait();const auto completed=Clock::now();
        void* mapped=nullptr;D3D12_RANGE range{0,queries.size()*sizeof(Result)},empty{0,0};
        Check(readback->Map(0,&range,&mapped));results.resize(queries.size());memcpy(results.data(),mapped,range.End);readback->Unmap(0,&empty);
        uint64_t times[4]{};range.End=sizeof(times);Check(timeReadback->Map(0,&range,&mapped));memcpy(times,mapped,sizeof(times));timeReadback->Unmap(0,&empty);
        const auto end=Clock::now();
        return {Ms(start,end),Ms(start,submitted),Ms(submitted,completed),(times[2]-times[1])*1000.0/frequency,
            (times[1]-times[0])*1000.0/frequency,(times[3]-times[2])*1000.0/frequency};
    }
};
static Json Stats(std::vector<double> values){
    std::sort(values.begin(),values.end());const auto n=values.size();
    return {{"mean_ms",std::accumulate(values.begin(),values.end(),0.0)/n},{"median_ms",values[n/2]},
        {"p95_ms",values[std::min(n-1,size_t(std::ceil(n*.95)-1))]},{"max_ms",values.back()}};
}
static void Validate(const std::vector<Result>& expected,const std::vector<Result>& actual){
    if(expected.size()!=actual.size())throw std::runtime_error("Output count mismatch");
    for(size_t i=0;i<expected.size();++i)
        if(expected[i].index!=actual[i].index || std::abs(expected[i].distance-actual[i].distance)>std::max(1.0f,expected[i].distance)*1e-5f)
            throw std::runtime_error("CPU/GPU nearest-point mismatch at query "+std::to_string(i));
}
static double ProcessCpuSeconds(){
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))throw std::runtime_error("GetProcessTimes failed");
    return (double((uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+
            double((uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime))/1e7;
}
class Pacer {
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
public:
    Pacer(){if(!timer)throw std::runtime_error("High-resolution waitable timer unavailable");}
    ~Pacer(){CloseHandle(timer);}
    void Until(Clock::time_point target){
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(target-Clock::now()).count();
        if(ns<=0)return;
        LARGE_INTEGER due{};due.QuadPart=-std::max<int64_t>(1,ns/100);
        if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,10000)!=WAIT_OBJECT_0)
            throw std::runtime_error("Benchmark pacing timer failed");
    }
};
static Json Sustain(Compute& gpu,unsigned seconds,bool useGpu){
    // Precompute reference answers before measuring. Alternate 32 moving-query
    // batches; validation remains in the loop, but no CPU search masks GPU cost.
    std::mt19937 rng(0x1204);std::uniform_int_distribution<int> coord(-32000,32000);
    std::vector<Point> points(65536);
    for(unsigned i=0;i<points.size();++i)points[i]={float(coord(rng)),float(coord(rng)),float(coord(rng)/10),float(i%4)};
    Tree tree(points);gpu.SetPoints(points);
    std::array<std::vector<Point>,32> input;
    std::array<std::vector<Result>,32> expected;
    for(unsigned b=0;b<input.size();++b)for(unsigned q=0;q<150;++q){
        const Point p{float(coord(rng)),float(coord(rng)),float(coord(rng)/10),float(q%4)};
        input[b].push_back(p);expected[b].push_back(tree.Query(p));
    }
    std::vector<double> total,kernel,wait,late;
    std::vector<Result> output(150);unsigned overruns=0,frame=0;
    Pacer pacer;const double cpuStart=ProcessCpuSeconds();const auto start=Clock::now();
    while(Ms(start,Clock::now())<seconds*1000.0){
        const auto deadline=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(frame/60.0));
        pacer.Until(deadline);
        const auto begin=Clock::now();late.push_back(Ms(deadline,begin));const unsigned b=frame%32;
        if(useGpu){const auto t=gpu.Run(input[b],output);total.push_back(t.total);kernel.push_back(t.kernel);wait.push_back(t.wait);}
        else {for(unsigned q=0;q<150;++q)output[q]=tree.Query(input[b][q]);total.push_back(Ms(begin,Clock::now()));}
        Validate(expected[b],output);
        if(Ms(start,Clock::now())>(frame+1)*1000.0/60)++overruns;
        ++frame;
    }
    const double elapsed=Ms(start,Clock::now())/1000;
    Json r={{"backend",useGpu?"gpu":"cpu_tree"},{"target_hz",60},{"pacer","high_resolution_waitable_timer"},{"candidates",65536},{"queries",150},
        {"elapsed_seconds",elapsed},{"batches",frame},{"late_completions",overruns},{"scheduler_lateness",Stats(late)},
        {"cpu_core_equivalents",(ProcessCpuSeconds()-cpuStart)/elapsed},{"complete_query_cost",Stats(total)},
        {"local_gpu_usage_bytes",gpu.LocalMemory()},{"correct",true}};
    if(useGpu){r["gpu_kernel"]=Stats(kernel);r["cpu_fence_wait"]=Stats(wait);}
    return r;
}
int main(int argc,char** argv)try{
    std::string output="bot_compute_benchmark.json";unsigned iterations=40,sustain=0;bool debug=false,useGpu=true;
    for(int i=1;i<argc;++i){std::string arg=argv[i];
        if(arg=="--output" && i+1<argc)output=argv[++i];
        else if(arg=="--iterations" && i+1<argc)iterations=unsigned(std::stoul(argv[++i]));
        else if(arg=="--debug")debug=true;
        else if(arg=="--sustain-seconds" && i+1<argc)sustain=unsigned(std::stoul(argv[++i]));
        else if(arg=="--sustain-cpu")useGpu=false;
        else throw std::runtime_error("Usage: bot_compute_bench [--output file] [--iterations 5..200] [--debug] [--sustain-seconds 1..30] [--sustain-cpu]");
    }
    if(iterations<5 || iterations>200)throw std::runtime_error("Iterations must be5..200");
    if(sustain>30)throw std::runtime_error("Sustained test limited to30 seconds");
    const auto initStart=Clock::now();Compute gpu(debug);
    Json report={{"adapter",gpu.adapterName},{"vram_bytes",gpu.videoMemory},{"device_setup_ms",Ms(initStart,Clock::now())},
        {"synthetic_only",true},{"native_bot_backend",false},{"debug_layer",debug},{"iterations",iterations},
        {"workload","Exact nearest point in matching synthetic category. Not polygon projection, connectivity BFS, visibility or movement."},
        {"pipeline","D3D12 compute: CPU upload/copy -> one64-lane group/query -> readback/fence -> CPU results"}};
    std::mt19937 random(0x1204);std::uniform_int_distribution<int> coordinate(-32000,32000);
    if(!sustain)for(unsigned count:{200u,4096u,65536u}){
        std::vector<Point> points(count);
        for(unsigned i=0;i<count;++i)points[i]={float(coordinate(random)),float(coordinate(random)),float(coordinate(random)/10),float(i%4)};
        points[1]=points[0]; // deterministic tie: lower input index must win
        const auto treeStart=Clock::now();Tree tree(points);const double treeSetup=Ms(treeStart,Clock::now());
        const double uploadSetup=gpu.SetPoints(points);
        for(unsigned batch:{1u,16u,100u,150u,200u}){
            std::vector<Point> queries(batch);std::vector<Result> brute(batch),indexed(batch),actual;
            std::vector<double> cpuLinear,cpuTree,total,submit,wait,kernel,upload,readback;
            for(unsigned iteration=0;iteration<iterations+5;++iteration){
                for(unsigned i=0;i<batch;++i)queries[i]={float(coordinate(random)),float(coordinate(random)),float(coordinate(random)/10),float(i%4)};
                if(iteration==0){queries[0]=points[0];if(batch>1)queries[1].group=99;}
                const auto a=Clock::now();
                for(unsigned q=0;q<batch;++q){brute[q]={};for(unsigned p=0;p<count;++p)Consider(queries[q],points[p],p,brute[q]);}
                const auto b=Clock::now();for(unsigned q=0;q<batch;++q)indexed[q]=tree.Query(queries[q]);const auto c=Clock::now();
                const auto timings=gpu.Run(queries,actual);Validate(brute,indexed);Validate(brute,actual);
                if(iteration<5)continue;
                cpuLinear.push_back(Ms(a,b));cpuTree.push_back(Ms(b,c));total.push_back(timings.total);submit.push_back(timings.submit);
                wait.push_back(timings.wait);kernel.push_back(timings.kernel);upload.push_back(timings.upload);readback.push_back(timings.readback);
            }
            Json sample={{"candidates",count},{"queries",batch},{"cpu_tree_build_ms",treeSetup},{"gpu_static_upload_ms",uploadSetup},
                {"static_point_bytes",count*sizeof(Point)},{"dynamic_upload_bytes",batch*sizeof(Point)},{"result_bytes",batch*sizeof(Result)},
                {"cpu_linear",Stats(cpuLinear)},{"cpu_tree",Stats(cpuTree)},{"gpu_round_trip",Stats(total)},{"cpu_submit",Stats(submit)},
                {"cpu_fence_wait",Stats(wait)},{"gpu_kernel",Stats(kernel)},{"gpu_upload",Stats(upload)},{"gpu_readback",Stats(readback)},
                {"correct",true}};
            report["samples"].push_back(sample);
            std::cout<<count<<" candidates / "<<batch<<" queries: CPUtree "<<sample["cpu_tree"]["median_ms"]
                <<"ms GPU "<<sample["gpu_kernel"]["median_ms"]<<"ms roundtrip "<<sample["gpu_round_trip"]["median_ms"]<<"ms\n"<<std::flush;
        }
    }
    if(sustain)report["sustained"]=Sustain(gpu,sustain,useGpu);
    report["debug_messages"]=gpu.Diagnostics();
    report["local_gpu_usage_bytes"]=gpu.LocalMemory();
    bool passed=true;for(const auto& message:report["debug_messages"])if(message["severity"].get<int>()<=int(D3D12_MESSAGE_SEVERITY_ERROR))passed=false;
    report["passed"]=passed;std::ofstream file(output);if(!file)throw std::runtime_error("Cannot open report");file<<report.dump(2)<<'\n';
    if(!passed)throw std::runtime_error("D3D12 validation errors; inspect report");
    std::cout<<"PASS: measured outputs match CPU reference; report "<<output<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
