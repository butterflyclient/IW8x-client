#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "signature_scanner.h"
#include "thirdparty/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using Json=nlohmann::json;
namespace fs=std::filesystem;
static std::string Hex(uint64_t n){std::ostringstream s;s<<"0x"<<std::hex<<n;return s.str();}
static std::string Sha256(sigscan::Bytes data){
    BCRYPT_ALG_HANDLE provider{};uint8_t hash[32]{};
    if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider unavailable");
    const auto status=BCryptHash(provider,nullptr,0,const_cast<PUCHAR>(data.data()),ULONG(data.size()),hash,sizeof(hash));
    BCryptCloseAlgorithmProvider(provider,0);
    if(status<0)throw std::runtime_error("SHA256 failed");
    std::string out;constexpr char hex[]="0123456789abcdef";
    for(auto b:hash){out+=hex[b>>4];out+=hex[b&15];}return out;
}
static uint64_t Number(const Json& j){
    if(j.is_number_unsigned())return j.get<uint64_t>();
    if(!j.is_string())throw std::runtime_error("RVA must be unsigned or a numeric string");
    const auto s=j.get<std::string>();size_t used=0;
    if(s.empty()||s[0]=='-')throw std::runtime_error("invalid RVA");
    auto n=std::stoull(s,&used,0);if(used!=s.size())throw std::runtime_error("invalid RVA");return n;
}
static void Write(const fs::path& path,const std::string& text){
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::failbit|std::ios::badbit);out<<text;
}
int main(int argc,char** argv)try {
    if(argc<4){std::cerr<<"signature_scan IMAGE MANIFEST REPORT [--header CANDIDATES.h] [--verify-source] [--mapped] [--base 0xVA]\n";return 1;}
    fs::path imagePath=argv[1],manifestPath=argv[2],reportPath=argv[3],headerPath;
    bool verify=false,mapped=false;uint64_t actualBase=0;
    for(int i=4;i<argc;++i){const std::string option=argv[i];
        if(option=="--verify-source")verify=true;
        else if(option=="--mapped")mapped=true;
        else if(option=="--header"&&i+1<argc)headerPath=argv[++i];
        else if(option=="--base"&&i+1<argc)actualBase=Number(Json(std::string(argv[++i])));
        else throw std::runtime_error("unknown or incomplete option: "+option);}
    auto same=[](const fs::path& a,const fs::path& b){return fs::weakly_canonical(a)==fs::weakly_canonical(b)||
        (fs::exists(a)&&fs::exists(b)&&fs::equivalent(a,b));};
    for(const auto& out:{reportPath,headerPath})if(!out.empty())for(const auto& in:{imagePath,manifestPath})
        if(same(out,in))throw std::runtime_error("output would overwrite input");
    if(!headerPath.empty()&&same(headerPath,reportPath))throw std::runtime_error("outputs must differ");
    auto size=fs::file_size(imagePath);if(size>2ull*1024*1024*1024)throw std::runtime_error("image larger than 2 GiB");
    std::vector<uint8_t> bytes(size);std::ifstream file(imagePath,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);
    if(size)file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
    const auto image=sigscan::Image::Parse(bytes,mapped?sigscan::Image::Layout::Mapped:sigscan::Image::Layout::File,actualBase);
    if(fs::file_size(manifestPath)>16*1024*1024)throw std::runtime_error("manifest exceeds 16 MiB");
    std::ifstream manifestFile(manifestPath);Json manifest=Json::parse(manifestFile);
    if(manifest.at("schema")!=1||!manifest.at("symbols").is_array()||manifest["symbols"].size()>10000)throw std::runtime_error("unsupported manifest schema/size");
    Json report={{"schema",1},{"image",fs::absolute(imagePath).string()},{"manifest",fs::absolute(manifestPath).string()},
        {"image_sha256",Sha256(bytes)},{"source_sha256",manifest.value("input_sha256",std::string{})},
        {"image_base",Hex(image.actualBase)},{"image_size",Hex(image.size)},{"timestamp",image.timestamp},{"layout",mapped?"mapped":"file"},
        {"notice","Candidates only: verify function ABI, object layouts and hook bytes before porting."},{"symbols",Json::array()}};
    std::set<std::string> names;unsigned matched=0,failed=0;
    std::ostringstream header;header<<"#pragma once\n#include <cstdint>\n// SCAN CANDIDATES ONLY. Not ABI-verified; do not auto-enable hooks.\nnamespace port_candidates {\n";
    for(const auto& symbol:manifest["symbols"]) {
        Json result={{"name",symbol.value("name",std::string{})}};
        try {
            const auto name=symbol.at("name").get<std::string>();
            if(name.empty()||!names.insert(name).second)throw std::runtime_error("empty/duplicate symbol name");
            if(symbol.contains("source_rva"))result["source_rva"]=symbol["source_rva"];
            if(!symbol.contains("pattern")) {result["status"]="unavailable";result["reason"]=symbol.value("reason","no exported signature");++failed;report["symbols"].push_back(result);continue;}
            const auto pattern=sigscan::Pattern::Parse(symbol.at("pattern").get<std::string>());
            const auto section=symbol.value("section",std::string("executable"));
            std::vector<uint64_t> hits;
            for(const auto& s:image.sections){
                const bool selected=section=="executable"?s.executable():section=="readable"?s.readable():s.name==section;
                if(!selected)continue;
                for(auto at:sigscan::Find(s.bytes,pattern,2-hits.size()))hits.push_back(s.rva+at);
                if(hits.size()>=2)break;
            }
            result["matches"]=Json::array();for(auto hit:hits)result["matches"].push_back(Hex(hit));
            if(hits.size()!=1){result["status"]=hits.empty()?"missing":"ambiguous";result["match_count_is_lower_bound"]=hits.size()==2;++failed;}
            else {
                sigscan::Rule rule;auto resolve=symbol.value("resolve",Json::object());const auto kind=resolve.value("kind",std::string("match"));
                if(kind=="rel32")rule.kind=sigscan::Rule::Kind::Rel32;
                else if(kind=="abs64")rule.kind=sigscan::Rule::Kind::Absolute64;
                else if(kind!="match")throw std::runtime_error("unsupported resolution kind");
                rule.offset=resolve.value("offset",int64_t{});rule.next=resolve.value("next",int64_t{});rule.add=resolve.value("add",int64_t{});
                if(kind=="rel32"&&!resolve.contains("next"))throw std::runtime_error("rel32 needs explicit instruction-end offset");
                const auto rva=sigscan::Resolve(image,hits.front(),rule);const auto* target=image.At(rva);
                const auto expectation=symbol.value("target",std::string("any"));
                if(expectation!="any"&&expectation!="executable"&&expectation!="readable")throw std::runtime_error("invalid target constraint");
                if((expectation=="executable"&&!target->executable())||(expectation=="readable"&&!target->readable()))throw std::runtime_error("target section permissions mismatch");
                if(verify&&(!symbol.contains("source_rva")||Number(symbol["source_rva"])!=rva)) {
                    result["status"]="source_mismatch";result["candidate_rva"]=Hex(rva);++failed;
                }else {
                    result["status"]="matched";result["rva"]=Hex(rva);result["va"]=Hex(sigscan::Add(image.actualBase,int64_t(rva)));
                    result["target_section"]=target->name;++matched;
                    // Name must be a C++ identifier; JSON remains authoritative.
                    const bool identifier=(std::isalpha(static_cast<unsigned char>(name[0]))||name[0]=='_')&&
                        std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});
                    if(identifier)header<<"inline constexpr uintptr_t "<<name<<" = "<<Hex(rva)<<";\n";
                }
            }
        }catch(const std::exception& e){result["status"]="invalid";result["reason"]=e.what();++failed;}
        report["symbols"].push_back(std::move(result));
    }
    report["matched"]=matched;report["unresolved"]=failed;report["complete"]=failed==0;header<<"}\n";
    Write(reportPath,report.dump(2)+"\n");if(!headerPath.empty())Write(headerPath,header.str());
    std::cout<<"Matched "<<matched<<"; unresolved "<<failed<<". Report: "<<reportPath.string()<<"\n";
    return failed?2:0;
}catch(const std::exception& e){std::cerr<<"signature_scan: "<<e.what()<<"\n";return 1;}
