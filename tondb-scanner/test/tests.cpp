#include "td/utils/port/signals.h"
#include "td/utils/OptionParser.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/check.h"

#include "crypto/vm/cp0.h"
#include "crypto/vm/boc.h"

#include "td/utils/tests.h"
#include "td/actor/actor.h"
#include "td/utils/base64.h"
#include "crypto/block/block.h"
#include "vm/cells/Cell.h"
#include "convert-utils.h"
#include "InterfaceDetectors.hpp"
#include "IndexData.h"


TEST(convert, to_raw_address) {
    auto address = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6ccgEBAQEAJAAAQ4ARhy+Ifz/haSyza6FGBWNSde+ZjHy+uBieS1O4PxaPVnA=")).move_as_ok()).move_as_ok();
    auto raw_address_serialized = convert::to_raw_address(vm::load_cell_slice_ref(address));
    CHECK(raw_address_serialized.is_ok());
    ASSERT_EQ("0:8C397C43F9FF0B49659B5D0A302B1A93AF7CCC63E5F5C0C4F25A9DC1F8B47AB3", raw_address_serialized.move_as_ok());
}

TEST(convert, to_raw_address_with_anycast) {
    // tx hash 692263ed0c02006a42c2570c1526dc0968e9ef36849086e7888599f5f7745f3b
    auto address = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6ccgEBAQEAKAAAS74kmhnMAhgIba/WWH4XFusx+cERuqOcvI+CfNEkWIYKL2ECy/pq")).move_as_ok()).move_as_ok();
    auto raw_address_serialized = convert::to_raw_address(vm::load_cell_slice_ref(address));
    CHECK(raw_address_serialized.is_ok());
    ASSERT_EQ("0:249A19CFF5961F85C5BACC7E70446EA8E72F23E09F34491621828BD840B2FE9A", raw_address_serialized.move_as_ok());
}

TEST(convert, to_std_address) {
    auto address = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6ccgEBAQEAJAAAQ4ARhy+Ifz/haSyza6FGBWNSde+ZjHy+uBieS1O4PxaPVnA=")).move_as_ok()).move_as_ok();
    auto std_address_serialized = convert::to_std_address(vm::load_cell_slice_ref(address));
    CHECK(std_address_serialized.is_ok());
    td::Bits256 addr;
    addr.from_hex(td::Slice("8C397C43F9FF0B49659B5D0A302B1A93AF7CCC63E5F5C0C4F25A9DC1F8B47AB3"));
    ASSERT_EQ(block::StdAddress(0, addr), std_address_serialized.move_as_ok());
}

TEST(convert, to_std_address_with_anycast) {
    auto address = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6ccgEBAQEAKAAAS74kmhnMAhgIba/WWH4XFusx+cERuqOcvI+CfNEkWIYKL2ECy/pq")).move_as_ok()).move_as_ok();
    auto std_address_serialized = convert::to_std_address(vm::load_cell_slice_ref(address));
    CHECK(std_address_serialized.is_ok());
    td::Bits256 addr;
    addr.from_hex(td::Slice("249A19CFF5961F85C5BACC7E70446EA8E72F23E09F34491621828BD840B2FE9A"));
    ASSERT_EQ(block::StdAddress(0, addr), std_address_serialized.move_as_ok());
}

TEST(TonDbScanner, JettonWalletDetector_parse_burn) {
  td::actor::Scheduler scheduler({1});
  auto watcher = td::create_shared_destructor([] { td::actor::SchedulerContext::get()->stop(); });

  block::StdAddress addr(std::string("EQDKC7jQ_tIJuYyrWfI4FIAN-hFHakG3GrATpOiqBVtsGOd5"));
  // message payload for tx xiOZW3mVbHkCtgLxQqXVAg4DIgxrTE3j9lHw6H3P/Yg=, correct layout
  auto message_payload = vm::load_cell_slice_ref(vm::std_boc_deserialize(td::base64_decode(
    td::Slice("te6cckEBAgEAOQABZllfB7xUbeTvz/ieq1AezMgZqAEPdvSWQKq0LY5UE5PZzQsh8ADqxh1H03XLREV/xoLz4QEAAaAxtO6I")).move_as_ok()).move_as_ok());

  auto transaction = schema::Transaction();
  transaction.account = block::StdAddress(std::string("EQCk6s76oduqQH_3Y3O7fxjWVoUXtD3Ev6-NbHjjDmfG1drE")); // jetton wallet
  transaction.in_msg = std::make_optional(schema::Message());
  transaction.in_msg->source = "0:87BB7A4B20555A16C72A09C9ECE68590F80075630EA3E9BAE5A222BFE34179F0"; // owner

  td::actor::ActorId<InsertManagerInterface> insert_manager;
  td::actor::ActorOwn<JettonWalletDetector> jetton_wallet_detector;
  td::actor::ActorOwn<InterfaceManager> interface_manager;
  td::actor::ActorOwn<JettonMasterDetector> jetton_master_detector;

  // prepare jetton metadata
  std::unordered_map<std::string, JettonWalletData> cache;
  JettonWalletData jetton_master;
  jetton_master.jetton = "0:BDF3FA8098D129B54B4F73B5BAC5D1E1FD91EB054169C3916DFC8CCD536D1000";
  cache.emplace(std::string("0:A4EACEFAA1DBAA407FF76373BB7F18D6568517B43DC4BFAF8D6C78E30E67C6D5"), jetton_master);

  scheduler.run_in_context([&] {
    interface_manager = td::actor::create_actor<InterfaceManager>("interface_manager", insert_manager);
    jetton_master_detector = td::actor::create_actor<JettonMasterDetector>("jetton_master_detector", interface_manager.get(), insert_manager);

    jetton_wallet_detector = td::actor::create_actor<JettonWalletDetector>("jetton_wallet_detector",
      jetton_master_detector.get(), interface_manager.get(), insert_manager, cache);

    auto P = td::PromiseCreator::lambda([&transaction, &jetton_master](td::Result<JettonBurn> R) {
      CHECK(R.is_ok());
      auto burn = R.move_as_ok();
      ASSERT_EQ(transaction.in_msg->source.value(), burn.owner);
      ASSERT_EQ(convert::to_raw_address(transaction.account), burn.jetton_wallet);
      ASSERT_EQ(jetton_master.jetton, burn.jetton_master);
      ASSERT_EQ(6083770390284902059, burn.query_id);
      ASSERT_TRUE(burn.custom_payload.not_null());
      ASSERT_EQ("A3FBB2B6DF19CEAA089D2794A26845822A2284D59B277469D167FFC19D00E44B", burn.custom_payload->get_hash().to_hex());
      CHECK(td::BigIntG<257>(8267792794) == **burn.amount.get());
    });
    td::actor::send_closure(jetton_wallet_detector, &JettonWalletDetector::parse_burn, transaction, message_payload, std::move(P));
    watcher.reset();
  });

  scheduler.run(10);
  scheduler.stop(); 
}

TEST(TonDbScanner, JettonWalletDetector_parse_burn_without_custom_payload) {
  /**
   * According to the TEP-74 (https://github.com/ton-blockchain/TEPs/blob/master/text/0074-jettons-standard.md) burn message should have the following layout:
   * burn#595f07bc query_id:uint64 amount:(VarUInteger 16)
   *           response_destination:MsgAddress custom_payload:(Maybe ^Cell)
   *           = InternalMsgBody;
   * In fact, the Maybe flag could be omitted at all and the referrence implementation ignores it (https://github.com/ton-blockchain/token-contract/blob/main/ft/jetton-wallet.fc#L170-L171)
   * So parser should be able to handle such cases since it is handled on the contract side.
   */
  td::actor::Scheduler scheduler({1});
  auto watcher = td::create_shared_destructor([] { td::actor::SchedulerContext::get()->stop(); });

  // message payload for tx Z2AuVEPZgtkFeLtneWg4i39xiOkEIDD9Lrhvz66amZU=
  auto message_payload = vm::load_cell_slice_ref(vm::std_boc_deserialize(td::base64_decode(
    td::Slice("te6cckEBAQEAFAAAI1lfB7wAAAAAAAAAAFGSVNOAAqKoE5Y=")).move_as_ok()).move_as_ok());

  auto transaction = schema::Transaction();
  transaction.account = block::StdAddress(std::string("EQCvlBsnlt-qAsiKtb9cb0IwFraA0bdTmj4K6bKbNQNjy1Cc")); // jetton wallet
  transaction.in_msg = std::make_optional(schema::Message());
  transaction.in_msg->source = "0:893B73E987E771F0E28326962C9373E4A02D2C90553D5E219383758159AAA7A6"; // owner

  td::actor::ActorId<InsertManagerInterface> insert_manager;
  td::actor::ActorOwn<JettonWalletDetector> jetton_wallet_detector;
  td::actor::ActorOwn<InterfaceManager> interface_manager;
  td::actor::ActorOwn<JettonMasterDetector> jetton_master_detector;

  // prepare jetton metadata
  std::unordered_map<std::string, JettonWalletData> cache;
  JettonWalletData jetton_master;
  jetton_master.jetton = "0:2DCBB7322C88C5ED4F8AD15806FE0724B1300AB482E46C908D8CF62567CE5C09";
  cache.emplace(std::string("0:AF941B2796DFAA02C88AB5BF5C6F423016B680D1B7539A3E0AE9B29B350363CB"), jetton_master);

  scheduler.run_in_context([&] {
    interface_manager = td::actor::create_actor<InterfaceManager>("interface_manager", insert_manager);
    jetton_master_detector = td::actor::create_actor<JettonMasterDetector>("jetton_master_detector", interface_manager.get(), insert_manager);

    jetton_wallet_detector = td::actor::create_actor<JettonWalletDetector>("jetton_wallet_detector",
      jetton_master_detector.get(), interface_manager.get(), insert_manager, cache);

    auto P = td::PromiseCreator::lambda([&transaction, &jetton_master](td::Result<JettonBurn> R) {
      CHECK(R.is_ok());
      auto burn = R.move_as_ok();
      ASSERT_EQ(transaction.in_msg->source.value(), burn.owner);
      ASSERT_EQ(convert::to_raw_address(transaction.account), burn.jetton_wallet);
      ASSERT_EQ(jetton_master.jetton, burn.jetton_master);
      ASSERT_EQ(0, burn.query_id);
      ASSERT_TRUE(burn.custom_payload.is_null());
      CHECK(td::BigIntG<257>(108000000000) == **burn.amount.get());
    });
    td::actor::send_closure(jetton_wallet_detector, &JettonWalletDetector::parse_burn, transaction, message_payload, std::move(P));
    watcher.reset();
  });

  scheduler.run(10);
  scheduler.stop(); 
}


TEST(TonDbScanner, JettonWalletDetector) {
  td::actor::Scheduler scheduler({1});
  auto watcher = td::create_shared_destructor([] { td::actor::SchedulerContext::get()->stop(); });

  block::StdAddress addr(std::string("EQDKC7jQ_tIJuYyrWfI4FIAN-hFHakG3GrATpOiqBVtsGOd5"));
  auto code_cell = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6cckECEgEAAzEAART/APSkE/S88sgLAQIBYgIDAgLLBAUAG6D2BdqJofQB9IH0gahhAgEgBgcCAWILDAIBSAgJAfH4Hpn/0AfSAQ+AH2omh9AH0gfSBqGCibUKkVY4L5cWCUYX/5cWEqGiE4KhAJqgoB5CgCfQEsZ4sA54tmZJFkZYCJegB6AGWAZJB8gDg6ZGWBZQPl/+ToAn0gegIY/QAQa6ThAHlxYjvADGRlgqgEZ4s4fQEL5bWJ5kCgC3QgxwCSXwTgAdDTAwFxsJUTXwPwEuD6QPpAMfoAMXHXIfoAMfoAMALTHyGCEA+KfqW6lTE0WfAP4CGCEBeNRRm6ljFERAPwEOA1ghBZXwe8upNZ8BHgXwSED/LwgAEV+kQwcLry4U2ACughAXjUUZyMsfGcs/UAf6AiLPFlAGzxYl+gJQA88WyVAFzCORcpFx4lAIqBOgggiYloCqAIIImJaAoKAUvPLixQTJgED7ABAjyFAE+gJYzxYBzxbMye1UAgEgDQ4AgUgCDXIe1E0PoA+kD6QNQwBNMfIYIQF41FGboCghB73ZfeuhKx8uLF0z8x+gAwE6BQI8hQBPoCWM8WAc8WzMntVIA/c7UTQ+gD6QPpA1DAI0z/6AFFRoAX6QPpAU1vHBVRzbXBUIBNUFAPIUAT6AljPFgHPFszJIsjLARL0APQAywDJ+QBwdMjLAsoHy//J0FANxwUcsfLiwwr6AFGooYIImJaAggiYloAStgihggiYloCgGKEn4w8l1wsBwwAjgDxARAOM7UTQ+gD6QPpA1DAH0z/6APpA9AQwUWKhUkrHBfLiwSjC//LiwoIImJaAqgAXoBe88uLDghB73ZfeyMsfyz9QBfoCIc8WUAPPFvQAyXGAGMjLBSTPFnD6AstqzMmAQPsAQBPIUAT6AljPFgHPFszJ7VSAAcFJ5oBihghBzYtCcyMsfUjDLP1j6AlAHzxZQB88WyXGAEMjLBSTPFlAG+gIVy2oUzMlx+wAQJBAjAA4QSRA4N18EAHbCALCOIYIQ1TJ223CAEMjLBVAIzxZQBPoCFstqEssfEss/yXL7AJM1bCHiA8hQBPoCWM8WAc8WzMntVLp4DOo=")).move_as_ok()).move_as_ok();
  auto data_cell = vm::std_boc_deserialize(td::base64_decode(td::Slice("te6cckECEwEAA3sAAY0xKctoASFZDXpO6Q6ZgXHilrBvG9KSTVMUJk1CMXwYaoCc9JirAC61IQRl0/la95t27xhIpjxZt32vl1QQVF2UgTNuvD18YAEBFP8A9KQT9LzyyAsCAgFiAwQCAssFBgAboPYF2omh9AH0gfSBqGECASAHCAIBYgwNAgFICQoB8fgemf/QB9IBD4AfaiaH0AfSB9IGoYKJtQqRVjgvlxYJRhf/lxYSoaITgqEAmqCgHkKAJ9ASxniwDni2ZkkWRlgIl6AHoAZYBkkHyAODpkZYFlA+X/5OgCfSB6Ahj9ABBrpOEAeXFiO8AMZGWCqARnizh9AQvltYnmQLALdCDHAJJfBOAB0NMDAXGwlRNfA/AS4PpA+kAx+gAxcdch+gAx+gAwAtMfIYIQD4p+pbqVMTRZ8A/gIYIQF41FGbqWMUREA/AQ4DWCEFlfB7y6k1nwEeBfBIQP8vCAARX6RDBwuvLhTYAK6CEBeNRRnIyx8Zyz9QB/oCIs8WUAbPFiX6AlADzxbJUAXMI5FykXHiUAioE6CCCJiWgKoAggiYloCgoBS88uLFBMmAQPsAECPIUAT6AljPFgHPFszJ7VQCASAODwCBSAINch7UTQ+gD6QPpA1DAE0x8hghAXjUUZugKCEHvdl966ErHy4sXTPzH6ADAToFAjyFAE+gJYzxYBzxbMye1UgD9ztRND6APpA+kDUMAjTP/oAUVGgBfpA+kBTW8cFVHNtcFQgE1QUA8hQBPoCWM8WAc8WzMkiyMsBEvQA9ADLAMn5AHB0yMsCygfL/8nQUA3HBRyx8uLDCvoAUaihggiYloCCCJiWgBK2CKGCCJiWgKAYoSfjDyXXCwHDACOAQERIA4ztRND6APpA+kDUMAfTP/oA+kD0BDBRYqFSSscF8uLBKML/8uLCggiYloCqABegF7zy4sOCEHvdl97Iyx/LP1AF+gIhzxZQA88W9ADJcYAYyMsFJM8WcPoCy2rMyYBA+wBAE8hQBPoCWM8WAc8WzMntVIABwUnmgGKGCEHNi0JzIyx9SMMs/WPoCUAfPFlAHzxbJcYAQyMsFJM8WUAb6AhXLahTMyXH7ABAkECMADhBJEDg3XwQAdsIAsI4hghDVMnbbcIAQyMsFUAjPFlAE+gIWy2oSyx8Syz/JcvsAkzVsIeIDyFAE+gJYzxYBzxbMye1U8/HTGA==")).move_as_ok()).move_as_ok();


  // the most tricky part is to craft this ShardStateUnsplit structure. This is one is a minumum one setup with
  // default values and without exotic cells to make it compatible with test environment
  auto mc_config_cell = vm::std_boc_deserialize(td::base64_decode(td::Slice("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")).move_as_ok()).move_as_ok();
  MasterchainBlockDataState mc_block;
  auto res = block::ConfigInfo::extract_config(mc_config_cell, block::ConfigInfo::needCapabilities | block::ConfigInfo::needLibraries);
  CHECK(res.is_ok());
  mc_block.config_ = std::move(res.move_as_ok());

  td::actor::ActorId<InsertManagerInterface> insert_manager;
  td::actor::ActorOwn<JettonWalletDetector> jetton_wallet_detector;
  td::actor::ActorOwn<InterfaceManager> interface_manager;
  td::actor::ActorOwn<JettonMasterDetector> jetton_master_detector;


  std::unordered_map<std::string, JettonMasterData> jetton_master_cache;
  JettonMasterData jetton_master_data;
  jetton_master_data.code_boc = "te6ccgECEwEABGMAART/APSkE/S88sgLAQIBYgIDAgLLBAUCA3pgDxAE99CDHAJJfA+AB0NMDAXGwkl8D4PpA+kAx+gAxcdch+gAx+gAwAtMf0z/tRNDUAdD6QPpA+kAwA/oA0wDTn9TUMBBHKsAV4wIqghBZXwe8uo4VNV8DNTVbNBLHBfLgSQH6QNQwEvAR4CqCEHvdl9664wIqghAsdrlzuuMCOYGBwgJAgHODQ4Aijk5OlGBxwXy4EkE+kDUMCDQgGDXIfoAMEuwUnzwEFAooAdEZVAjyFAHzxZQBc8WUAPPFsnIUAb6AssAy58TzBLMzMntVAH+Ojs7BfoA+kD4KFQSCnBUIBNUFAPIUAT6AljPFgHPFszJIsjLARL0APQAywDJ+QBwdMjLAsoHy//J0FAKxwXy4EoUoUVGVBIDSpnIUAfPFlAFzxZQA88WychQBvoCywDLnxPMEszMye1U9ASCEDr9RhxwgBjIywVQBc8WJPoCFAoB/jpfBwSCCJiWgKAVvPLgSwL6QNMAMJXIIc8WyZFt4oIQ0XNUAHCAGMjLBVAFzxYk+gIUy2oTyx8Uyz8j+kQwcLqOM/goRANwVCATVBQDyFAE+gJYzxYBzxbMySLIywES9AD0AMsAyfkAcHTIywLKB8v/ydDPFpZsInABywHi9AALAf47OyeCEGLdhvG6jjAyNlFhxwXy4EkC+kAwRnVQI8hQB88WUAXPFlADzxbJyFAG+gLLAMufE8wSzMzJ7VTgJ4IQZkR9rbqONjM0NVFUxwXy4ElwyMsBydAXEEZEFVAjyFAHzxZQBc8WUAPPFsnIUAb6AssAy58TzBLMzMntVOAnDAB6y2oTyx9SMMs/9ADJgED7APpAMCDXCwHDAI4fghDVMnbbcIAQyMsFUAPPFiL6AhLLassfyz/JgEL7AJFb4gAKyYBA+wAA/IIQEdtAirqOMDE2UWHHBfLgSQL6QDBGdFAjyFAHzxZQBc8WUAPPFsnIUAb6AssAy58TzBLMzMntVOA5BoIQdD+OWLqOMlFhxwUHwAAXsfLgSXEC1DBHBwPIUAfPFlAFzxZQA88WychQBvoCywDLnxPMEszMye1U4F8JhA/y8ACVDP4KAFwVCATVBQDyFAE+gJYzxYBzxbMySLIywES9AD0AMsAySD5AHB0yMsCygfL/8nQd4AYyMsFWM8Wggr68ID6AstrzMzJcfsAgAIsM/goAXBUIBNUFAPIUAT6AljPFgHPFszJIsjLARL0APQAywDJ+QBwdMjLAsoHy//J0HGAGMjLBVjPFnD6AstqzMmAQPsAgAJutvPaiaGoA6H0gfSB9IBgB/QBpgGnP6moYCCO2OPwUALgqEAmqCgHkKAJ9ASxniwDni2ZkkWRlgIl6AHoAZYBk/IA4OmRlgWUD5f/k6EACASAREgA8q0btRNDUAdD6QPpA+kAwA/oA0wDTn9TUMBBHW2wkAECqLe1E0NQB0PpA+kD6QDAD+gDTANOf1NQwEEdsQn9VIA==";
  jetton_master_data.data_boc = "te6ccgECIwEABQgAAzNUQx/4R70Fw0yOMQxZtg6M6lF09YZxsDdaRAECAwCGgBvHK+ossgcO4Jb+paMLINzWCCPx4kJ9i6Aw5SrJ1F+xhACVcKobt9eK6KgKdpkVp/xK23My7U0DHJUrko/WuH5dmwEDAMAEART/APSkE/S88sgLEgIBIAUGAUO/8ILrZjtXoAGS9KasRnKI3y3+3bnaG+4o9lIci+vSHx7ABwIBIAgJAKYAaHR0cHM6Ly9yYXcuZ2l0aHVidXNlcmNvbnRlbnQuY29tL29yYml0LWNoYWluL2JyaWRnZS10b2tlbi1pbWFnZS9tYWluL3Rvbi91c2RjLnBuZwIBIAoLAgEgDg8BQb9FRqb/4bec/dhrrT24dDE9zeL7BeanSqfzVS2WF8edEwwBQb9u1PlCp4SM4ssGa3ehEoxqH/jEP0OKLc4kYSup/6uLAw0ANABPcmJpdCBCcmlkZ2UgVG9uIFVTRCBDb2luAAwAb1VTREMBQb9SCN70b1odT53OZqswn0qFEwXxZvke952SPvWONPmiCRABQb9dAfpePAaQHEUEbGst3Opa92T+oO7XKhDUBPIxLOskfREATABPcmJpdCBCcmlkZ2UgVG9rZW4gb24gVE9OIGJsb2NrY2hhaW4hAAQANgIBYhMUAgLLFRYAG6D2BdqJofQB9IH0gahhAgEgFxgCAWIcHQIBSBkaAfH4Hpn/0AfSAQ+AH2omh9AH0gfSBqGCibUKkVY4L5cWCUYX/5cWEqGiE4KhAJqgoB5CgCfQEsZ4sA54tmZJFkZYCJegB6AGWAZJB8gDg6ZGWBZQPl/+ToAn0gegIY/QAQa6ThAHlxYjvADGRlgqgEZ4s4fQEL5bWJ5kGwC3QgxwCSXwTgAdDTAwFxsJUTXwPwEuD6QPpAMfoAMXHXIfoAMfoAMALTHyGCEA+KfqW6lTE0WfAP4CGCEBeNRRm6ljFERAPwEOA1ghBZXwe8upNZ8BHgXwSED/LwgAEV+kQwcLry4U2ACughAXjUUZyMsfGcs/UAf6AiLPFlAGzxYl+gJQA88WyVAFzCORcpFx4lAIqBOgggiYloCqAIIImJaAoKAUvPLixQTJgED7ABAjyFAE+gJYzxYBzxbMye1UAgEgHh8AgUgCDXIe1E0PoA+kD6QNQwBNMfIYIQF41FGboCghB73ZfeuhKx8uLF0z8x+gAwE6BQI8hQBPoCWM8WAc8WzMntVIA/c7UTQ+gD6QPpA1DAI0z/6AFFRoAX6QPpAU1vHBVRzbXBUIBNUFAPIUAT6AljPFgHPFszJIsjLARL0APQAywDJ+QBwdMjLAsoHy//J0FANxwUcsfLiwwr6AFGooYIImJaAggiYloAStgihggiYloCgGKEn4w8l1wsBwwAjgICEiAOM7UTQ+gD6QPpA1DAH0z/6APpA9AQwUWKhUkrHBfLiwSjC//LiwoIImJaAqgAXoBe88uLDghB73ZfeyMsfyz9QBfoCIc8WUAPPFvQAyXGAGMjLBSTPFnD6AstqzMmAQPsAQBPIUAT6AljPFgHPFszJ7VSAAcFJ5oBihghBzYtCcyMsfUjDLP1j6AlAHzxZQB88WyXGAEMjLBSTPFlAG+gIVy2oUzMlx+wAQJBAjAA4QSRA4N18EAHbCALCOIYIQ1TJ223CAEMjLBVAIzxZQBPoCFstqEssfEss/yXL7AJM1bCHiA8hQBPoCWM8WAc8WzMntVA==";
  jetton_master_data.address = "0:BAD48411974FE56BDE6DDBBC612298F166DDF6BE5D504151765204CDBAF0F5F1";
  jetton_master_cache.emplace(std::string("0:BAD48411974FE56BDE6DDBBC612298F166DDF6BE5D504151765204CDBAF0F5F1"), jetton_master_data);

  auto P = td::PromiseCreator::lambda([](td::Result<JettonWalletData> R) {
    CHECK(R.is_ok());
    auto jetton_wallet = R.move_as_ok();
    ASSERT_EQ("0:BAD48411974FE56BDE6DDBBC612298F166DDF6BE5D504151765204CDBAF0F5F1", jetton_wallet.jetton);
    ASSERT_EQ("0:90AC86BD2774874CC0B8F14B58378DE94926A98A1326A118BE0C35404E7A4C55", jetton_wallet.owner);
    CHECK(td::BigIntG<257>(1219766) == **jetton_wallet.balance.get());
  });

  scheduler.run_in_context([&] {
    interface_manager = td::actor::create_actor<InterfaceManager>("interface_manager", insert_manager);
    jetton_master_detector = td::actor::create_actor<JettonMasterDetector>("jetton_master_detector", interface_manager.get(), insert_manager, jetton_master_cache);

    jetton_wallet_detector = td::actor::create_actor<JettonWalletDetector>("jetton_wallet_detector",
      jetton_master_detector.get(), interface_manager.get(), insert_manager);

    td::actor::send_closure(jetton_wallet_detector, &JettonWalletDetector::detect, addr, code_cell, data_cell, 0ll, 0, mc_block, std::move(P));
    watcher.reset();
  });

  scheduler.run(10); 
}



int main(int argc, char **argv) {
  td::set_default_failure_signal_handler().ensure();
    auto &runner = td::TestsRunner::get_default();
  runner.run_all();
  return 0;
}