/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include "helpers.h"
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
#include <string>

static std::vector<std::string> component_files = {
    "add.wasm",
    "complex_with_host.wasm",
    "complex.wasm",
    "logging_service.component.wasm",
    "processor_and_logging_merged_wac_plug.wasm",
    "processor_service.component.wasm",
    "sampletypes.wasm"
};

class BinaryParserTest : public testing::Test
{
  public:
    std::unique_ptr<ComponentHelper> helper;
    BinaryParserTest() {}
    ~BinaryParserTest() {}
  
    virtual void SetUp() {
        helper = std::make_unique<ComponentHelper>();
        helper->do_setup();
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

TEST_F(BinaryParserTest, TestAllComponentsLoadAndUnload)
{
    // Load and unload every listed component
    for (const std::string &name : component_files) {
        helper->reset_component();
        std::string path = name;
        printf("LoadAndUnloadComponent: %s\n", path.c_str());
        bool ret = helper->read_wasm_file(path.c_str());
        ASSERT_TRUE(ret);
        ret = helper->load_component();
        ASSERT_TRUE(ret);
        ASSERT_TRUE(helper->is_loaded());

        // Unload component and free raw buffer to simulate full unload
        helper->reset_component();
        if (helper->component_raw) {
            BH_FREE(helper->component_raw);
            helper->component_raw = NULL;
        }
        ASSERT_FALSE(helper->is_loaded());
        ASSERT_EQ(helper->get_section_count(), 0u);
    }
}

TEST_F(BinaryParserTest, TestLoadCorruptComponent)
{
    // Corrupt header and expect load failure
    helper->reset_component();
    bool ret = helper->read_wasm_file((std::string("add.wasm").c_str()));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);
    helper->component_raw[0] ^= 0xFF; // corrupt
    ret = helper->load_component();
    ASSERT_FALSE(ret);
}

TEST_F(BinaryParserTest, TestDecodeHeaderValid)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("logging_service.component.wasm");
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);

    WASMHeader header;
    bool ok = wasm_decode_header(helper->component_raw,
                                           helper->wasm_file_size,
                                           &header);
    ASSERT_TRUE(ok);
    ASSERT_EQ(header.magic, WASM_MAGIC_NUMBER);
    ASSERT_EQ(header.version, WASM_COMPONENT_VERSION);
    ASSERT_EQ(header.layer, WASM_COMPONENT_LAYER);
}

TEST_F(BinaryParserTest, TestDecodeHeaderInvalid)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file("logging_service.component.wasm");
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);

    std::vector<uint8_t> corrupted(helper->component_raw,
                                   helper->component_raw + helper->wasm_file_size);
    corrupted[0] ^= 0xFF; // corrupt magic byte

    WASMHeader header;
    ret = wasm_decode_header(corrupted.data(),
                                           (uint32_t)corrupted.size(),
                                           &header);
    ASSERT_TRUE(ret);
    ASSERT_FALSE(is_wasm_component(header));
}

TEST_F(BinaryParserTest, TestSectionAliasIndividual)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file((std::string("add.wasm").c_str()));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);
    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->is_loaded());

    std::string check_against = "test:project/my-interface@0.1.0#add";

    auto sections = helper->get_section(WASM_COMP_SECTION_ALIASES);
    bool found = false;
    for (auto section: sections) {
        ASSERT_EQ(section->id, WASM_COMP_SECTION_ALIASES);

        WASMComponentAliasSection *alias_section = section->parsed.alias_section;
        for (uint32_t id = 0; id < alias_section->count; id++) {
            WASMComponentAliasDefinition* alias_def = &alias_section->aliases[id];

            if (alias_def->alias_target_type == WASM_COMP_ALIAS_TARGET_CORE_EXPORT) {
                if (std::string{alias_def->target.core_exported.name->name} == check_against) {
                    found = true;
                }
            }
        }
    }

    ASSERT_TRUE(found);
}