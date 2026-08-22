/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "helpers.h"


// -------- helpers to inspect real components --------



bool component_has_core_imports(const WASMComponent *c) {
  if (!c) return false;
  for (uint32_t i = 0; i < c->section_count; ++i) {
    const WASMComponentSection *sec = &c->sections[i];
    if (sec->id == WASM_COMP_SECTION_CORE_MODULE) {
      const WASMComponentCoreModuleWrapper *wrap = sec->parsed.core_module;
      if (wrap && wrap->module && wrap->module->import_count > 0)
        return true;
    } else if (sec->id == WASM_COMP_SECTION_COMPONENT) {
      if (component_has_core_imports(sec->parsed.component))
        return true;
    }
  }
  return false;
}

bool component_has_section(const WASMComponent* c, uint8_t id) {
  if (!c) return false;
  for (uint32_t i = 0; i < c->section_count; ++i) {
    const WASMComponentSection* s = &c->sections[i];
    if (s->id == id) return true;
    if (s->id == WASM_COMP_SECTION_COMPONENT && s->parsed.component) {
      if (component_has_section(s->parsed.component, id)) return true;
    }
  }
  return false;
}

bool component_is_simple_enough(const WASMComponent* c, const char** why) {
  if (component_has_core_imports(c)) { if (why) *why = "has core-module imports"; return false; }
  if (component_has_section(c, WASM_COMP_SECTION_CORE_MODULE)) { if (why) *why = "has core modules"; return false; }
  if (component_has_section(c, WASM_COMP_SECTION_CORE_INSTANCE)) { if (why) *why = "has core instances"; return false; }
  if (component_has_section(c, WASM_COMP_SECTION_INSTANCES)) { if (why) *why = "has component instances"; return false; }
  if (component_has_section(c, WASM_COMP_SECTION_ALIASES)) { if (why) *why = "has aliases"; return false; }
  if (component_has_section(c, WASM_COMP_SECTION_IMPORTS)) { if (why) *why = "has component imports"; return false; }
  return true; // “simple” component
}

WASMComponent* load_component_from_candidates_internal(const char *file_name, const char *test_dir_name) {
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  if (!file_name) {
      printf("Invalid file\n");
      return NULL;
  }

  const char *substr = "wasm-micro-runtime";
  /* Use the LAST occurrence: hosted CI checks the repo out under
     .../work/wasm-micro-runtime/wasm-micro-runtime/..., and anchoring on the
     first match truncates the prefix at the parent directory, producing a
     fixture path one component short. */
  char *pos = NULL;
  for (char *p = strstr(cwd, substr); p; p = strstr(p + 1, substr))
      pos = p;
  if (!pos) {
      printf("Could not find 'wasm-micro-runtime' in cwd\n");
      return NULL;
  }

  size_t prefix_len = (pos - cwd) + strlen(substr);
  char full_path[prefix_len + strlen("/tests/unit/") + strlen(test_dir_name) + strlen("/wasm-apps/") + strlen(file_name) + 1];

  full_path[0] = '\0';
  strncat(full_path, cwd, prefix_len);
  strcat(full_path, "/tests/unit/");
  strcat(full_path, test_dir_name);
  strcat(full_path, "/wasm-apps/");
  strcat(full_path, file_name);
  const char* candidates[] = {
    file_name,
    full_path // absolute path necessary for running debugger
  };

  unsigned char *buf = nullptr; uint32_t size = 0; const char *path = nullptr;
  for (const char *p : candidates) {
    buf = (unsigned char*)bh_read_file_to_buffer(p, &size);
    if (buf) { path = p; break; }
  }
  if (!buf) return nullptr;

  auto *comp = (WASMComponent *)wasm_runtime_malloc(sizeof(WASMComponent));
  if (!comp) { BH_FREE(buf); return nullptr; }
  memset(comp, 0, sizeof(WASMComponent));

  if (!wasm_decode_header(buf, size, &comp->header) || !is_wasm_component(comp->header)) {
    wasm_runtime_free(comp); BH_FREE(buf); return nullptr;
  }

  LoadArgs args{}; char name_buf[32] = {0};
  memset(&args, 0, sizeof(LoadArgs));
  std::snprintf(name_buf, sizeof(name_buf), "%s", "Real Component");
  args.name = name_buf;
  args.wasm_binary_freeable = false;
  args.clone_wasm_binary = false;
  args.no_resolve = true;
  args.is_component = true;

  if (!wasm_component_parse_sections(buf, size, comp, &args, 0)) {
    wasm_runtime_free(comp); BH_FREE(buf); return nullptr;
  }

  return comp;
}

// Pretty name for section ids
const char* section_name(uint8_t id) {
  switch (id) {
    case WASM_COMP_SECTION_CORE_CUSTOM:   return "core_custom(0)";
    case WASM_COMP_SECTION_CORE_MODULE:   return "core_module(1)";
    case WASM_COMP_SECTION_CORE_INSTANCE: return "core_instance(2)";
    case WASM_COMP_SECTION_CORE_TYPE:     return "core_type(3)";
    case WASM_COMP_SECTION_COMPONENT:     return "component(4)";
    case WASM_COMP_SECTION_INSTANCES:     return "instances(5)";
    case WASM_COMP_SECTION_ALIASES:       return "aliases(6)";
    case WASM_COMP_SECTION_TYPE:          return "type(7)";
    case WASM_COMP_SECTION_CANONS:        return "canons(8)";
    case WASM_COMP_SECTION_START:         return "start(9)";
    case WASM_COMP_SECTION_IMPORTS:       return "imports(10)";
    case WASM_COMP_SECTION_EXPORTS:       return "exports(11)";
    case WASM_COMP_SECTION_VALUES:        return "values(12)";
    default:                              return "unknown";
  }
}

const char* core_kind_name(uint8_t kind) {
  switch (kind) {
    case 0:   return "core func";
    case 1:   return "core table";
    case 2:   return "core memory";
    case 3:   return "core global";
    default:  return "unknown";
  }
}

void print_core_module_imports(WASMModule *core_module, uint32 level = 0) {
  if (!core_module) {
    printf("Core module not loaded correctly!\n");
    return;
  }
  uint32 idx;
  printf("%*c %d Imports:\n", level, '-', core_module->import_count);
  for (idx = 0; idx < core_module->import_count; idx++) {
    printf("%*c module name: %s, field name: %s, kind: %s\n",level, '-', core_module->imports[idx].u.names.module_name, core_module->imports[idx].u.names.field_name, core_kind_name(core_module->imports[idx].kind)) ;
  }
  printf("%*c %d Import functions:\n", level, '-', core_module->import_function_count);
  for (idx = 0; idx < core_module->import_function_count; idx++) {
    printf("%*c module name: %s, field name: %s, kind: %s\n",level, '-', core_module->import_functions[idx].u.names.module_name, core_module->import_functions[idx].u.names.field_name, core_kind_name(core_module->import_functions[idx].kind)) ;
    printf("Func param count: %d\n", core_module->import_functions[idx].u.function.func_type->param_count);
  }
  printf("\n");

  printf("%*c %d Exports:\n", level, '-', core_module->export_count);
  for (idx = 0; idx < core_module->export_count; idx++) {
    printf("%*c name: %s, kind: %s\n",level, '-', core_module->exports[idx].name, core_kind_name(core_module->exports[idx].kind)) ;
  }

  return;
}

void print_component_sections(WASMComponent *comp, uint32 level) {
  
  printf("Component sections=%u\n", comp->section_count);
  uint32 core_module_count = 0;
  for (uint32_t i = 0; i < comp->section_count; ++i) {
    uint8_t id = comp->sections[i].id;
    printf("%*c  %u: %s\n",level, '-', i, section_name(id));
    if (id == 4) {
      WASMComponent *inner_comp = comp->sections[i].parsed.component;
      print_component_sections(inner_comp, level + 3);
    }
    if (id == 1) {
      // Add print core module sections
      WASMModule *core_module = (WASMModule *)comp->sections[i].parsed.core_module->module_handle;
      if(core_module) {
        printf("CORE MODULE %d\n", core_module_count);
        core_module_count++;
      }
      else {
        printf("Core module NOT loaded!\n");
      }
      printf("%*c  Module has %d imports\n",level + 2,'-',  core_module->import_count);
      printf("%*c  Module has %d exports\n",level + 2,'-',  core_module->export_count);
      print_core_module_imports(core_module, level + 2);
    }
  }
  return;
}

// Load the .wasm bytes from one of several likely paths
unsigned char* load_wasm(uint32_t &out_size, const char* &used_path) {
    const char* candidates[] = {
        "surface_and_geometry.wasm",
        "component-instantiation/surface_and_geometry.wasm",
        "../../../unit/component-instantiation/wasm_apps/surface_and_geometry.wasm"
    };
    for (const char* p : candidates) {
        unsigned char* buf = (unsigned char*)bh_read_file_to_buffer(p, &out_size);
        if (buf) { used_path = p; return buf; }
    }
    used_path = nullptr;
    return nullptr;
}

  // Parse the component (header + sections) into a WASMComponent*
WASMComponent* parse_component(const unsigned char* buf, uint32_t size) {
    auto* comp = (WASMComponent*)wasm_runtime_malloc(sizeof(WASMComponent));
    if (!comp) return nullptr;
    std::memset(comp, 0, sizeof(WASMComponent));

    if (!wasm_decode_header(buf, size, &comp->header)) {
        wasm_runtime_free(comp);
        return nullptr;
    }
    if (!is_wasm_component(comp->header)) {
        wasm_runtime_free(comp);
        return nullptr;
    }

    LoadArgs args{};
    memset(&args, 0, sizeof(LoadArgs));
    char name_buf[32]; std::memset(name_buf, 0, sizeof(name_buf));
    std::snprintf(name_buf, sizeof(name_buf), "%s", "Real Component");
    args.name = name_buf;
    args.wasm_binary_freeable = false;
    args.clone_wasm_binary = false;
    args.no_resolve = false;
    args.is_component = true;

    if (!wasm_component_parse_sections(buf, size, comp, &args, 0)) {
        wasm_runtime_free(comp);
        return nullptr;
    }
    return comp;
}
