#pragma once
// SOURCE: verified numeric 2300 armor-plate ScriptFile, 1,557-byte installed
// compiler input. SHA-256: 6fe10af264d09db022b8f294b5a09648ca8998a9e6cdfe510e6f8c04b64ed1cd
// TARGET: only byte1249 changes (method ID 0x8746 -> Replay 0x8743).
namespace
{
    constexpr std::string_view island_armor_source_hex =
		"3b4b157a660700006026509c5b003e5c3d7201003b3a7e8d86720900957a6e7f00003a123b3cfeffffffa772070004feffffff0d87153c082300001603883c08"
		"230000160288157adb5a0000601831da602a207d5b015c7a2d5500001f8f001601107213003cb82200005b023e3f021ef9002494260000003cb8220000160502"
		"30025b033c082300005c3e203d0001013f40881ef900408b4002010103013c082300005c3e203d0018b8220000153cb8220000983a0100581516017a954c0000"
		"606879a758157adb190000606916aa58157a316400006048a28b58157af814000060495a7f7205007565235758957a112a00003a123b4b3cb82200001605025b"
		"003c082300005c3e203d005b0116327aff1500001f8f0016011f3f015b023f3e881ef9003e8b01013c082300005c3f203d005b033e3cb8220000537201003b3e"
		"18b8220000153cb8220000980a0000583ba601003e16963c08230000888b5b013e1ef9007a861100003a80e48258153e1ef9007aea060000603c49b0583ba602"
		"00017afe0a00003a2b3e5c107201003b3cfeffffffa87201003b3a7edc817201003b4f0805000e01003b7a602a00001e41035b02153a7a381b0000600411455b"
		"033f47006dfeffffff87151601980a020058483000000058155c1a1a1a1a895c02003e3f6032b28e5b043b4b7a694d00003a2b7a4e1c00003a2b4f0a06005848"
		"7a000000585c18feffffff157a453700007adb4100007a872500007a270e00007a8d7f00007afc36000060349e7858160118feffffff4802070000583b4b7a7e"
		"0b00003a2b7afa3e00003a2b3c10010000a7190c003c10010000002c7e000000a71911003c10010000002c7e0000007a692f0000977213003a7ecb8172080095"
		"7ae62000003a12791b3f003c10010000a7190c003c10010000002c7e000000a71911003c10010000002c7e0000007ac2190000107213003a7ecb81720800957a"
		"244600003a12791b3f00957a493f00003a123ba60100153e776009586a58153e776009a91658153e77606e6fa158153e7760418f9658153e776053d63158153e"
		"7760495f1b583e18feffffff3ba601007ad86600003a2b7a363c00003a2b4f100200771c07003cfeffffffa8597201003b1dd7005b013e700080e844025b0270"
		"0000fa445b03700080e8445b045c5b0542417672250047052cfeffffffa7771c0c0047052cfeffffff3a7e2681977201003b791dd70001041b2b004f0af9ff58"
		"403f4e66e803885b063e5d300730083009300a4f8602007284007aa90d00003c4e470000390103157a075b00006079412d010241a719020040a7190300405c0a"
		"190b003cb82200003c08230000767245001dd7007000409c440201011dd7003f76722000470a2cfeffffffa7771c0c00470a2cfeffffff3a7e2681977201003b"
		"791b28004ff8f7ff58700000803e243e5d940500000094030000001b8b00957a096e00003a123b4b3c0ed50000a7721b00153a3c0ed5000060739b1e5b003e7a"
		"e8490000107202005c2f03013a7e8b861c05003a7ecb81595b013cfeffffffa81c07003c66720000a8595b023e756b25041931003cf4c30000002ceac0000000"
		"2c4aa100007a16790000971917003cf4c30000002ceac00000002c4aa100007a51420000970f243f1c02003e597202005c2f3cb82200003c0823000010721300"
		"157b18230000002cfeffffff607c9558585c2f16012f3b4b754b1dd41905003a7e4687595b003cfeffffffa71907003cfeffffff5c0a5b013f1c02003e592f3b"
		"4b7a264100007a901000003a803982587a2f4f00007a6b7f00003a803982587a5d1200007ac87600003a803982587a2f1900007ab60c00003a803982587ade04"
		"00007a613b00003a803982587a563500007ab13f00003a803982583b4b7ade0500007ac95900003a803a82587a242800007a696600003a803a82587a8b580000"
		"7a864000003a803a82587ae13d00007a9d1400003a803a82587af21700007a835b00003a803a82587a963b00007abb2000003a803a82583b4b7a201000003a2b"
		"957aa71800003a124f26ffff583c10010000a7190c003c10010000002c7e000000a71911003c10010000002c7e0000007a8732000010720400791b3000793cfe"
		"ffffffa87206005c18feffffff155c9886f9ff583b";

    std::vector<std::uint8_t> island_armor_source()
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(island_armor_source_hex.size()/2);
        const auto digit = [](const char c) -> std::uint8_t
        {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            throw std::runtime_error("corrupted pinned armor GSC fixture");
        };
        for (std::size_t i = 0; i < island_armor_source_hex.size(); i += 2)
            bytes.push_back(static_cast<std::uint8_t>(16*digit(island_armor_source_hex[i]) + digit(island_armor_source_hex[i+1])));
        return bytes;
    }

    void island_armor_translation_checks()
    {
        namespace abi = gsc::island_armor_native_abi;
        const auto src = island_armor_source();
        Check(src.size() == abi::source_length && abi::fingerprint(src) == abi::source_fingerprint,
            "installed armor source profile changed");
        const auto reject = [](std::string_view name, std::vector<std::uint8_t> bytes)
        {
            const auto old = bytes;
            Check(!abi::translate_private_compiler_copy(name, bytes) && bytes == old,
                "unknown armor GSC accepted or partially modified");
        };
        reject("2301", src);
        reject("scripts/mp/equipment/armor_plate", src);
        reject(abi::source_module, {});
        auto short_code=src;
        short_code.pop_back();
        reject(abi::source_module, short_code);
        auto long_code=src;
        long_code.push_back(0);
        reject(abi::source_module, long_code);
        for (const std::size_t pos : {std::size_t(0),src.size()-1,std::size_t(1248),
            std::size_t(1249),std::size_t(1250),std::size_t(430)})
        {
            auto changed=src;
            changed[pos]^=1;
            reject(abi::source_module, changed);
        }
        auto out=src;
        Check(abi::translate_private_compiler_copy(abi::source_module,out),
            "verified Island plate script was rejected");
        Check(abi::fingerprint(out)==abi::replay_fingerprint &&
            out[abi::source_method_opcode]==0x7e &&
            out[abi::source_method_opcode+1]==0x43 && out[abi::source_method_opcode+2]==0x87,
            "armor plate method was not translated to exact Replay ID");
        std::size_t changed=0;
        for (std::size_t i=0;i<out.size();++i)
        {
            if (src[i]==out[i]) continue;
            Check(i==abi::source_method_opcode+1,"armor translation modified unrelated GSC byte");
            ++changed;
        }
        Check(changed==1,"armor translation must replace exactly one byte");
        reject(abi::source_module,out);
        auto rollback=src;
        Check(!abi::translate_exact_profile(abi::source_module,rollback,
            abi::source_fingerprint,abi::replay_fingerprint^1) && rollback==src,
            "armor translation failed transactional rollback");
    }
}
