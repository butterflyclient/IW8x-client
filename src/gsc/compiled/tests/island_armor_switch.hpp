#pragma once
// Pinned installed numeric2245 source GSC, 1,379 bytes.
// Change ONLY the 3-argument IsSameWeapon native call at opcode1312.
namespace
{
    constexpr std::string_view island_armor_switch_source_hex =
        "3ba6040001020340a70e0400510101023ea70e02005c243e51013fa840413a839081583ba601003e3a7fdf83583ba601003e3a7fe083583ba60200015c5b0240"
        "1e6203720c003a7e6585401024940a0000003a7e65851e42034010243e722000405b033e1e1b0177190500411e620359720500411e420324413a7f6885580301"
        "403a7fe283583ba601007ada4d00003a2b7ad90900003a2b30015c24153f98d608000e26003a7e29855b023e1e6403771907003c100100003e107208005c0101"
        "94040000001601010103013e7205009404000000791b3e00153f98bafeff583b4b4f2a0300583a7e65855b003e1e64037202001a2f3e2f3b4b4fccffffa72f3b"
        "a601003e1e1b017205003e1e6d03244fa0ffff5b013ea71903003e3f10190700153f98ce0700a52f3ba60200013f3a7f97810e02005c2f751555e00e02005c2f"
        "3ea87719050075589b30a5190500753e7046a57202005c2f4f0effff5b023ea7725100401ee6005b035c5b043f7a66040000101c07003f7ad1180000101c0700"
        "3f7a563b0000101c07003f7a697b0000107208001601249410000000401e66027a7b2f0000107203001601243e0e02005c2f0302154098a606007202005c2f16"
        "012f3ba601003a7e65853e107206003e3a7f688558153e98c0fcff583b3ba6030001027a4e6800003a2b7ab22500003a2b40a70e02005c2f153e40985afeff0e"
        "02005c2f4fe6fdff7209004fa8fdff3a7f688558403a7f6785583fa872070015409818fcff58154098c205007208004f8e00005816012f403a7f6685771c0600"
        "403a7f9781a57202005c2f7552034c771c14003ea87719050075378ef1a519050075781e96a559720800403a7f6885585c2f791b50003b4b3a7e26815b003e3a"
        "7f66857207003e3a7f6885583b3ba60200017a1f0400003a2b7a8b0900003a2b3f3a7f97810e02005c2f153f98ba04000e0a00153f9804fbff5816012f4f74fc"
        "ff720400791b0b00153f987e04000e0a00153f98c8faff5816012f153e3c2678000098f6fdff5b023a1e0201720b001d65033a7fe0835816012415409876faff"
        "58957a286d00003a123e0e05004f1000005816012f3ba601007abf1600003a2b7a405200003a2b30013a7e26811e640372d6013a7edc811c05004f7afbff5972"
        "06007994be0100003f243c66720000a8729c007af84400005b02157a521600007a49110000607f1502721300153a157a1f2f00007a232d0000604f8ae94a2415"
        "7a470700007af9380000604800c2725100153a157a171700007aaf7e00006054eb724a5b033ea87236003c26780000a7190a003c267800003a7f978159720c00"
        "3c26780000010194140000004fb002005b043e275c0a7205005c63000102030103013e1e410301010301940f0100004f6a02005b043fa7771c06003f3a7f9781"
        "a57249003c26780000a7771c11003c26780000002c7e0000007a135c000010720700030194e00000003c267800003a7f9781720c003c267800000101940c0000"
        "003e275c0a7205005c630001013fa7190a003f1ee6007a4c2f0000101907003f3a7f81815c101905003e271601107212007aac5c000047016d7e000000879479"
        "0000007a413d00003a7f9781726d003e27160110720a005c63000101945b0000003e27160210192e007ad63b00003a7f97811c09007acb0f00003a7f97811c09"
        "007ac35200003a7f97811c0a007a840300003a7f9781597222005c6300002c7e0000007ab621000010720a005c6300010194060000001601630001010301153e"
        "98baf9ff58791be0013ba601003e1e1b017205003e1e6d032416013e3a7e26812064032f3b4b6a5b003a7e89815b013e5b023e1e990030035b043ea7721e003e"
        "6302010147012c110200000e06003f42278e04873e401faf00241b23008c0211422f3b";

    std::vector<std::uint8_t> island_armor_switch_source()
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(island_armor_switch_source_hex.size()/2);
        const auto nibble=[](const char c)->std::uint8_t
        {
            if(c>='0' && c<='9') return static_cast<std::uint8_t>(c-'0');
            if(c>='a' && c<='f') return static_cast<std::uint8_t>(c-'a'+10);
            throw std::runtime_error("corrupt Island switch GSC fixture");
        };
        for(std::size_t i=0;i<island_armor_switch_source_hex.size();i+=2)
            bytes.push_back(static_cast<std::uint8_t>(nibble(island_armor_switch_source_hex[i])*16 + nibble(island_armor_switch_source_hex[i+1])));
        return bytes;
    }

    void island_armor_switch_translation_checks()
    {
        namespace abi=gsc::island_armor_switch_native_abi;
        const auto src=island_armor_switch_source();
        Check(src.size()==abi::source_length && abi::fingerprint(src)==abi::source_fingerprint,
            "installed Source switch script changed");
        const auto reject=[](std::string_view name,std::vector<std::uint8_t> code)
        {
            const auto old=code;
            Check(!abi::translate_private_compiler_copy(name,code) && code==old,
                "switch ABI accepted or partially mutated unknown source");
        };
        reject("2246",src);
        reject("scripts/cp_mp/utility/inventory_utility",src);
        reject(abi::source_module,{});
        auto short_src=src;
        short_src.pop_back();reject(abi::source_module,short_src);
        auto long_src=src;
        long_src.push_back(0);reject(abi::source_module,long_src);
        for(const std::size_t pos : {std::size_t(0),src.size()-1,std::size_t(172),
                std::size_t(237),std::size_t(751),std::size_t(813),
                std::size_t(1312),std::size_t(1313),std::size_t(1314)})
        {
            auto invalid=src;
            invalid[pos]^=1;
            reject(abi::source_module,invalid);
        }
        auto output=src;
        Check(abi::translate_private_compiler_copy(abi::source_module,output),
            "authentic Source inventory switch rejected");
        Check(abi::fingerprint(output)==abi::replay_fingerprint && output[1313]==0x63,
            "IsSameWeapon method not restored");
        std::size_t changed=0;
        for(std::size_t i=0;i<src.size();++i)
        {
            if(src[i]==output[i]) continue;
            Check(i==1313,"switch ABI changed an unrelated byte");
            ++changed;
        }
        Check(changed==1,"switch ABI must change exactly one byte");
        // These three calls were correctly converted from one-argument
        // Source IsNullWeapon ID0x365 to Replay IsNullWeapon ID0x364.
        for(const std::size_t pos : {std::size_t(172),std::size_t(237),std::size_t(813)})
        {
            Check(output[pos]==0x1e && output[pos+1]==0x64 && output[pos+2]==0x03,
                "legitimate one-argument null weapon check mutated");
        }
        reject(abi::source_module,output);
        auto rollback=src;
        Check(!abi::translate_exact_profile(abi::source_module,rollback,
            abi::source_fingerprint,abi::replay_fingerprint^1) && rollback==src,
            "switch ABI failed to roll back target hash mismatch");
    }
}
