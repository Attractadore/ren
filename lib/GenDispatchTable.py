#!/usr/bin/env python3
import xml.etree.ElementTree as ET
from sys import argv
from pathlib import Path
from collections import defaultdict


class Argument:
    def __init__(self, argtype, name):
        self.argtype = argtype
        self.name = name

    def __str__(self):
        return f"{self.argtype} {self.name}"


class Function:
    def __init__(self, name, ret=None, args=None):
        self.name = name
        self.ret = ret
        self.args = args


def parse_core_version_or_extension(node, out: dict):
    # Skip platform specifics since this would mess with ABI
    if "platform" in node.attrib:
        return

    extension_name = node.attrib["name"]
    for r in filter(lambda r: r.tag == "require", node):
        cmd_requires = [extension_name]
        if feature := r.attrib.get("feature", None):
            cmd_requires.append(feature)
        if extensions := r.attrib.get("extension", None):
            for extension in extensions.split(','):
                cmd_requires.append(extension)

        for f in filter(lambda f: f.tag == "command", r):
            cmd_name = f.attrib["name"]
            out[cmd_name].append(cmd_requires)


def parse_core_function_requirements(root, out: dict):
    for c in filter(lambda c: c.tag == "feature", root):
        parse_core_version_or_extension(c, out)


def parse_extension_function_requirements(root, out: dict):
    for e in root.find("extensions"):
        parse_core_version_or_extension(e, out)


def parse_type(e):
    tp = e.find("type")
    name = tp.text
    tail = tp.tail.rstrip()
    pref = e.text if e.text else ""
    return f"{pref}{name}{tail}"


def parse_all(root):
    func_reqs = defaultdict(list)
    parse_core_function_requirements(root, func_reqs)
    parse_extension_function_requirements(root, func_reqs)

    aliases = {}
    cmds = {}
    for cmd in root.find("commands"):
        if alias_target := cmd.attrib.get("alias", None):
            cmd_name = cmd.attrib["name"]
            aliases[cmd_name] = alias_target
            continue

        proto = cmd.find("proto")
        cmd_name = proto.find("name").text
        ret_type = parse_type(proto)
        args = [Argument(parse_type(e), e.find("name").text)
                for e in filter(lambda p: p.tag == "param" and p.attrib.get("api", "vulkan") == "vulkan", cmd)]

        cmds[cmd_name] = Function(cmd_name, ret_type, args)

    for alias, target in aliases.items():
        targetf = cmds[target]
        cmds[alias] = Function(alias, targetf.ret, targetf.args)

    req_funcs = defaultdict(list)
    for f, r in func_reqs.items():
        c = " || ".join((" && ".join((ext for ext in ext_group))
                        for ext_group in r))
        req_funcs[c].append(f)

    return (cmds, req_funcs)


def is_instance_cmd(cmd):
    return cmd.args[0].argtype == "VkInstance" or cmd.name in ("vkGetDeviceProcAddr", "vkCreateInstance")


def is_physical_device_cmd(cmd):
    return cmd.args[0].argtype == "VkPhysicalDevice"


def is_device_cmd(cmd):
    return not is_instance_cmd(cmd) and not is_physical_device_cmd(cmd)


def generate_table(cmds, table_name, req_funcs):
    load_code = "\n".join((
        "\n".join((
            f"#if {conds}",
            "\n".join(f"    PFN_{cmd} {cmd[2:]};" for cmd in cmds),
            f"#endif // {conds}",
        )) for conds, cmds in req_funcs.items()))
    return "\n".join((
        "typedef struct {",
        load_code,
        f"}} {table_name};",
    ))


def generate_load_table_proto(
        table_name, loadf_name, procf, handle, handle_name):
    proc = "proc"
    table = "table"
    return f"void {loadf_name}(PFN_{procf} {proc}, {handle} {handle_name}, {table_name}* {table});"


def generate_load_instance_table_proto(table_name, loadf_name):
    return generate_load_table_proto(
        table_name, loadf_name, "vkGetInstanceProcAddr", "VkInstance",
        "instance")


def generate_load_device_table_proto(table_name, loadf_name):
    return generate_load_table_proto(
        table_name, loadf_name, "vkGetDeviceProcAddr", "VkDevice", "device")


def generate_load_table(all_cmds, table_name, loadf_name, req_funcs, procf,
                        handle, handle_name):
    proc = "proc"
    table = "table"
    load_code = "\n".join(
        "\n".join((
            f"#if {conds}",
            "\n".join(
                f"    {table}->{cmd[2:]} = (PFN_{cmd}) {proc}({handle_name}, \"{cmd}\");" for cmd in cmds if cmd in all_cmds),
            f"#endif // {conds}",
        )) for conds, cmds in req_funcs.items() if any(map(lambda cmd: cmd in all_cmds, cmds))
    )
    return "\n".join((f"void {loadf_name}(PFN_{procf} {proc}, {handle} {handle_name}, {table_name}* {table}) {{",
                      load_code,
                      "};",
                      ))


def generate_load_instance_table(
        instance_cmds, table_name, loadf_name, req_funcs):
    return generate_load_table(
        instance_cmds, table_name, loadf_name, req_funcs,
        "vkGetInstanceProcAddr", "VkInstance", "instance")


def generate_load_device_table(device_cmds, table_name, loadf_name, req_funcs):
    return generate_load_table(
        device_cmds, table_name, loadf_name, req_funcs, "vkGetDeviceProcAddr",
        "VkDevice", "device")


def format_mixin_command(
        cmd, mixin_type, get_table, get_handle, get_allocator):
    allocator = "const VkAllocationCallbacks*"
    Derived = "Derived"
    impl = "impl"
    name = cmd.name[2:]
    replace_args = {
        mixin_type: f"{impl}->{get_handle}()",
        allocator: f"{impl}->{get_allocator}()",
    }
    args = [arg for arg in cmd.args if arg.argtype not in replace_args]
    args = ', '.join((str(arg) for arg in args))
    call_args = [replace_args.get(arg.argtype, arg.name) for arg in cmd.args]
    call_args = ', '.join(call_args)
    return "\n".join((
        f"   {cmd.ret} {name}({args}) const {{",
        f"      const auto* {impl} = static_cast<const {Derived}*>(this);",
        f"      auto* func = {impl}->{get_table}().{name};",
        f"      assert(func && \"vk{name} not loaded!\");",
        f"      return func({call_args});",
        f"   }}",
    ))


def generate_mixin(all_cmds, mixin_name, req_funcs, mixin_type, get_handle):
    Derived = "Derived"
    mixin_code = "\n\n".join("\n".join((
        f"#if {conds}",
        "\n\n".join(
            format_mixin_command(
                all_cmds[cmd],
                mixin_type, "getDispatchTable",
                get_handle, "getAllocator")
            for cmd in cmds if cmd in all_cmds),
        f"#endif // {conds}",
    )) for conds, cmds in req_funcs.items() if any(map(lambda cmd: cmd in all_cmds, cmds)))
    return "\n".join((
        f"template <class {Derived}>",
        f"struct {mixin_name} {{",
        mixin_code,
        "};",
    ))


def generate_instance_mixin(instance_cmds, mixin_name, req_funcs):
    return generate_mixin(
        instance_cmds, mixin_name, req_funcs, "VkInstance", "getInstance")


def generate_physical_device_mixin(device_cmds, mixin_name, req_funcs):
    return generate_mixin(
        device_cmds, mixin_name, req_funcs, "VkPhysicalDevice",
        "getPhysicalDevice")


def generate_device_mixin(device_cmds, mixin_name, req_funcs):
    return generate_mixin(
        device_cmds, mixin_name, req_funcs, "VkDevice", "getDevice")


def main():
    vk_xml = Path(argv[1])
    h_out = Path(argv[2])
    hpp_out = Path(argv[3])
    c_out = Path(argv[4])
    header = h_out.name

    table_name = "DispatchTable"
    load_instance_f = "loadInstanceFunctions"
    load_device_f = "loadDeviceFunctions"
    instance_mixin = "InstanceFunctionsMixin"
    device_mixin = "DeviceFunctionsMixin"
    physical_device_mixin = "PhysicalDeviceFunctionsMixin"

    tree = None
    with open(vk_xml, "rb") as f:
        tree = ET.parse(vk_xml)
    root = tree.getroot()

    cmds, req_funcs = parse_all(root)
    instance_cmds = {cmd_name: cmd for cmd_name, cmd in cmds.items(
    ) if is_instance_cmd(cmd) or is_physical_device_cmd(cmd)}
    device_cmds = {cmd_name: cmd for cmd_name,
                   cmd in cmds.items() if is_device_cmd(cmd)}

    extern_c_begin = "\n".join((
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif // __cplusplus",
    ))

    extern_c_end = "\n".join((
        "#ifdef __cplusplus",
        "}",
        "#endif // __cplusplus",
    ))

    h_out.parent.mkdir(exist_ok=True, parents=True)
    with open(h_out, "w") as h:
        h.write("\n".join((
            "#pragma once",
            "#include <vulkan/vulkan.h>",
            "",
            extern_c_begin,
            "",
            generate_table(cmds, table_name, req_funcs),
            "",
            generate_load_instance_table_proto(table_name, load_instance_f),
            generate_load_device_table_proto(table_name, load_device_f),
            "",
            extern_c_end,
        )))

    c_out.parent.mkdir(exist_ok=True, parents=True)
    with open(c_out, "w") as c:
        c.write("\n".join((
            f"#include \"{header}\"",
            "",
            extern_c_begin,
            "",
            generate_load_instance_table(
                instance_cmds, table_name, load_instance_f, req_funcs),
            "",
            generate_load_device_table(
                device_cmds, table_name, load_device_f, req_funcs),
            "",
            extern_c_end,
        )))

    physical_device_cmds = {cmd_name: cmd for cmd_name,
                            cmd in instance_cmds.items()
                            if is_physical_device_cmd(cmd)}
    instance_cmds = {
        cmd_name: cmd for cmd_name, cmd in instance_cmds.items()
        if is_instance_cmd(cmd)}

    hpp_out.parent.mkdir(exist_ok=True, parents=True)
    with open(hpp_out, "w") as hpp:
        hpp.write("\n".join((
            "#pragma once",
            f"#include \"{header}\"",
            "",
            "#include <cassert>",
            "",
            generate_instance_mixin(instance_cmds, instance_mixin, req_funcs),
            "",
            generate_physical_device_mixin(
                physical_device_cmds, physical_device_mixin, req_funcs),
            "",
            generate_device_mixin(device_cmds, device_mixin, req_funcs),
        )))


if __name__ == "__main__":
    main()
