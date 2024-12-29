#include <pugixml.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <format>
#include <vector>
#include <algorithm>
#include <tuple>
#include <filesystem>

bool waylandEnums = false;
bool clientCode   = false;
bool noInterfaces = false;

struct SRequestArgument {
    std::string wlType;
    std::string interface;
    std::string enumName;
    std::string name;
    bool        newType   = false;
    bool        allowNull = false;
};

struct SWaylandFunction {
    std::vector<SRequestArgument> args;
    std::string                   name;
    std::string                   since;
    std::string                   newIdType  = ""; // client only
    bool                          destructor = false;
};

struct SInterface {
    std::vector<SWaylandFunction> requests;
    std::vector<SWaylandFunction> events;
    std::string                   name;
    int                           version = 1;
};

struct SEnum {
    std::string                              name;
    std::string                              nameOriginal;
    std::vector<std::pair<std::string, int>> values;
};

struct {
    std::vector<SInterface> ifaces;
    std::vector<SEnum>      enums;
} XMLDATA;

std::string sanitize(const std::string& in) {
    if (in == "namespace")
        return "namespace_";
    if (in == "class")
        return "class_";
    if (in == "delete")
        return "delete_";
    if (in == "new")
        return "new_";
    return in;
}

std::string argsToShort(std::vector<SRequestArgument>& args, const std::string& since) {
    std::string shortt = since;
    for (auto& a : args) {
        if (a.wlType == "int")
            shortt += "i";
        else if (a.wlType == "new_id") {
            if (a.interface.empty())
                shortt += "su";
            shortt += "n";
        } else if (a.wlType == "uint")
            shortt += "u";
        else if (a.wlType == "fixed")
            shortt += "f";
        else if (a.wlType == "string")
            shortt += std::string(a.allowNull ? "?s" : "s");
        else if (a.wlType == "object")
            shortt += std::string(a.allowNull ? "?" : "") + "o";
        else if (a.wlType == "array")
            shortt += "a";
        else if (a.wlType == "fd")
            shortt += "h";
        else
            throw std::runtime_error("Unknown arg in argsToShort");
    }
    return shortt;
}

std::string camelize(std::string snake) {
    std::string result = "";
    for (size_t i = 0; i < snake.length(); ++i) {
        if (snake[i] == '_' && i != 0 && i + 1 < snake.length() && snake[i + 1] != '_') {
            result += ::toupper(snake[i + 1]);
            i++;
            continue;
        }

        result += snake[i];
    }

    return result;
}

std::string WPTypeToCType(const SRequestArgument& arg, bool event /* events pass iface ptrs, requests ids */, bool ignoreTypes = false /* for dangerous */) {
    if (arg.wlType == "uint" || arg.wlType == "new_id") {
        if (arg.enumName.empty() && arg.interface.empty())
            return "uint32_t";

        // enum
        if (!arg.enumName.empty()) {
            for (auto& e : XMLDATA.enums) {
                if (e.nameOriginal == arg.enumName)
                    return e.name;
            }
            return "uint32_t";
        }

        if (!event && clientCode && arg.wlType == "new_id")
            return "wl_proxy*";

        // iface
        if (!arg.interface.empty() && event) {
            for (auto& i : XMLDATA.ifaces) {
                if (i.name == arg.interface)
                    return camelize((clientCode ? "CC_" : "C_") + arg.interface + "*");
            }
            return "wl_resource*";
        }

        return "uint32_t";
    }
    if (arg.wlType == "object") {
        if (!arg.interface.empty() && event && !ignoreTypes) {
            for (auto& i : XMLDATA.ifaces) {
                if (i.name == arg.interface)
                    return camelize((clientCode ? "CC_" : "C_") + arg.interface + "*");
            }
        }
        return "wl_resource*";
    }
    if (arg.wlType == "int" || arg.wlType == "fd")
        return "int32_t";
    if (arg.wlType == "fixed")
        return "wl_fixed_t";
    if (arg.wlType == "array")
        return "wl_array*";
    if (arg.wlType == "string")
        return "const char*";
    throw std::runtime_error("unknown wp type");
    return "";
}

std::string HEADER;
std::string SOURCE;

struct {
    std::string name;
    std::string nameOriginal;
    std::string fileName;
} PROTO_DATA;

void parseXML(pugi::xml_document& doc) {

    for (auto& ge : doc.child("protocol").children("enum")) {
        SEnum enum_;
        enum_.nameOriginal = ge.attribute("name").as_string();
        enum_.name         = waylandEnums ? "enum " + PROTO_DATA.name + "_" + enum_.nameOriginal : camelize(PROTO_DATA.name + "_" + enum_.nameOriginal);
        for (auto& entry : ge.children("entry")) {
            auto VALUENAME = enum_.nameOriginal + "_" + entry.attribute("name").as_string();
            std::transform(VALUENAME.begin(), VALUENAME.end(), VALUENAME.begin(), ::toupper);
            enum_.values.emplace_back(std::make_pair<>(VALUENAME, entry.attribute("value").as_int()));
        }
        XMLDATA.enums.push_back(enum_);
    }

    for (auto& iface : doc.child("protocol").children("interface")) {
        SInterface ifc;
        ifc.name    = iface.attribute("name").as_string();
        ifc.version = iface.attribute("version").as_int();

        for (auto& en : iface.children("enum")) {
            SEnum enum_;
            enum_.nameOriginal = en.attribute("name").as_string();
            enum_.name         = waylandEnums ? "enum " + ifc.name + "_" + enum_.nameOriginal : camelize(ifc.name + "_" + enum_.nameOriginal);
            for (auto& entry : en.children("entry")) {
                auto VALUENAME = ifc.name + "_" + enum_.nameOriginal + "_" + entry.attribute("name").as_string();
                std::transform(VALUENAME.begin(), VALUENAME.end(), VALUENAME.begin(), ::toupper);
                enum_.values.emplace_back(std::make_pair<>(VALUENAME, entry.attribute("value").as_int()));
            }
            XMLDATA.enums.push_back(enum_);
        }

        for (auto& rq : iface.children("request")) {
            SWaylandFunction srq;
            srq.name       = rq.attribute("name").as_string();
            srq.since      = rq.attribute("since").as_string();
            srq.destructor = rq.attribute("type").as_string() == std::string{"destructor"};

            for (auto& arg : rq.children("arg")) {
                SRequestArgument sargm;
                if (arg.attribute("type").as_string() == std::string{"new_id"} && clientCode)
                    srq.newIdType = arg.attribute("interface").as_string();

                sargm.newType   = arg.attribute("type").as_string() == std::string{"new_id"} && clientCode;
                sargm.name      = sanitize(arg.attribute("name").as_string());
                sargm.wlType    = arg.attribute("type").as_string();
                sargm.interface = arg.attribute("interface").as_string();
                sargm.enumName  = arg.attribute("enum").as_string();
                sargm.allowNull = arg.attribute("allow-null").as_string() == std::string{"true"};

                srq.args.push_back(sargm);
            }

            ifc.requests.push_back(srq);
        }

        for (auto& ev : iface.children("event")) {
            SWaylandFunction sev;
            sev.name       = ev.attribute("name").as_string();
            sev.since      = ev.attribute("since").as_string();
            sev.destructor = ev.attribute("type").as_string() == std::string{"destructor"};

            for (auto& arg : ev.children("arg")) {
                SRequestArgument sargm;
                sargm.name      = sanitize(arg.attribute("name").as_string());
                sargm.interface = arg.attribute("interface").as_string();
                sargm.wlType    = arg.attribute("type").as_string();
                sargm.enumName  = arg.attribute("enum").as_string();
                sargm.allowNull = arg.attribute("allow-null").as_string() == std::string{"true"};

                sev.args.push_back(sargm);
            }

            ifc.events.push_back(sev);
        }

        XMLDATA.ifaces.push_back(ifc);
    }
}

void parseHeader() {

    // add some boilerplate
    HEADER += std::format(R"#(#pragma once

#include <functional>
#include <cstdint>
#include <string>
{}

#define F std::function

{}

)#",
                          (clientCode ? "#include <wayland-client.h>" : "#include <wayland-server.h>"),
                          (clientCode ? "struct wl_proxy;\n#define wl_resource wl_proxy" : "struct wl_client;\nstruct wl_resource;"));

    // parse all enums
    if (!waylandEnums) {
        for (auto& en : XMLDATA.enums) {
            HEADER += std::format("enum {} : uint32_t {{\n", en.name);
            for (auto& [k, v] : en.values) {
                HEADER += std::format("    {} = {},\n", k, v);
            }
            HEADER += "};\n\n";
        }
    }

    // fw declare all classes
    for (auto& iface : XMLDATA.ifaces) {
        const auto IFACE_CLASS_NAME_CAMEL = camelize((clientCode ? "CC_" : "C_") + iface.name);
        HEADER += std::format("\nclass {};", IFACE_CLASS_NAME_CAMEL);

        for (auto& rq : iface.requests) {
            for (auto& arg : rq.args) {
                if (!arg.interface.empty()) {
                    HEADER += std::format("\nclass {};", camelize((clientCode ? "CC_" : "C_") + arg.interface));
                }
            }
        }

        for (auto& rq : iface.events) {
            for (auto& arg : rq.args) {
                if (!arg.interface.empty()) {
                    HEADER += std::format("\nclass {};", camelize((clientCode ? "CC_" : "C_") + arg.interface));
                }
            }
        }
    }

    HEADER += "\n\n#ifndef HYPRWAYLAND_SCANNER_NO_INTERFACES\n";

    for (auto& iface : XMLDATA.ifaces) {
        const auto IFACE_WL_NAME       = iface.name + "_interface";
        const auto IFACE_WL_NAME_CAMEL = camelize(iface.name + "_interface");

        HEADER += std::format("extern const wl_interface {};\n", IFACE_WL_NAME, IFACE_WL_NAME_CAMEL, IFACE_WL_NAME);
    }

    HEADER += "\n#endif\n";

    for (auto& iface : XMLDATA.ifaces) {
        const auto IFACE_NAME_CAMEL       = camelize(iface.name);
        const auto IFACE_CLASS_NAME_CAMEL = camelize((clientCode ? "CC_" : "C_") + iface.name);

        if (!clientCode) {
            HEADER += std::format(R"#(
struct {}DestroyWrapper {{
    wl_listener listener;
    {}* parent = nullptr;
}};
            )#",
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);
        }

        // begin the class
        HEADER +=
            std::format(R"#(

class {} {{
  public:
    {}({});
    ~{}();

)#",
                        IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, (clientCode ? "wl_resource*" : "wl_client* client, uint32_t version, uint32_t id"), IFACE_CLASS_NAME_CAMEL);

        if (!clientCode) {
            HEADER += std::format(R"#(
    // set a listener for when this resource is _being_ destroyed
    void setOnDestroy(F<void({}*)> handler) {{
        onDestroy = handler;
    }}

    // set the data for this resource
    void setData(void* data) {{
        pData = data;
    }}

    // get the data for this resource
    void* data() {{
        return pData;
    }}

    // get the raw wl_resource ptr
    wl_resource* resource() {{
        return pResource;
    }}

    // get the client
    wl_client* client() {{
        return wl_resource_get_client(pResource);
    }}

    // send an error
    void error(uint32_t error, const std::string& message) {{
        wl_resource_post_error(pResource, error, "%s", message.c_str());
    }}

    // send out of memory
    void noMemory() {{
        wl_resource_post_no_memory(pResource);
    }}

    // get the resource version
    int version() {{
        return wl_resource_get_version(pResource);
    }}
            )#",
                                  IFACE_CLASS_NAME_CAMEL);
        } else {
            HEADER += R"#(
    // set the data for this resource
    void setData(void* data) {{
        pData = data;
    }}

    // get the data for this resource
    void* data() {{
        return pData;
    }}

    // get the raw wl_resource (wl_proxy) ptr
    wl_resource* resource() {{
        return pResource;
    }}

    // get the resource version
    int version() {{
        return wl_proxy_get_version(pResource);
    }}
            )#";
        }

        // add all setters for requests
        HEADER += "\n    // --------------- Requests --------------- //\n\n";

        for (auto& rq : (clientCode ? iface.events : iface.requests)) {

            std::string args = ", ";
            for (auto& arg : rq.args) {
                if (arg.newType)
                    continue;
                args += WPTypeToCType(arg, false) + ", ";
            }

            args.pop_back();
            args.pop_back();

            HEADER += std::format("    void {}(F<void({}*{})> handler);\n", camelize("set_" + rq.name), IFACE_CLASS_NAME_CAMEL, args);
        }

        // start events

        HEADER += "\n    // --------------- Events --------------- //\n\n";

        for (auto& ev : (!clientCode ? iface.events : iface.requests)) {
            std::string args = "";
            for (auto& arg : ev.args) {
                if (arg.newType)
                    continue;
                args += WPTypeToCType(arg, true) + ", ";
            }

            if (!args.empty()) {
                args.pop_back();
                args.pop_back();
            }

            HEADER += std::format("    {} {}({});\n", ev.newIdType.empty() ? "void" : "wl_proxy*", camelize("send_" + ev.name), args);
        }

        // dangerous ones
        if (!clientCode) {
            for (auto& ev : (!clientCode ? iface.events : iface.requests)) {
                std::string args = "";
                for (auto& arg : ev.args) {
                    if (arg.newType)
                        continue;
                    args += WPTypeToCType(arg, true, true) + ", ";
                }

                if (!args.empty()) {
                    args.pop_back();
                    args.pop_back();
                }

                HEADER += std::format("    void {}({});\n", camelize("send_" + ev.name + "_raw"), args);
            }
        }

        // end events

        // start private section
        HEADER += "\n  private:\n";

        // start requests storage
        HEADER += "    struct {\n";

        for (auto& rq : (clientCode ? iface.events : iface.requests)) {

            std::string args = ", ";
            for (auto& arg : rq.args) {
                if (arg.newType)
                    continue;
                args += WPTypeToCType(arg, false) + ", ";
            }

            if (!args.empty()) {
                args.pop_back();
                args.pop_back();
            }

            HEADER += std::format("        F<void({}*{})> {};\n", IFACE_CLASS_NAME_CAMEL, args, camelize(rq.name));
        }

        // end requests storage
        HEADER += "    } requests;\n";

        // constant resource stuff
        if (!clientCode) {
            HEADER += std::format(R"#(
    void onDestroyCalled();

    F<void({}*)> onDestroy;

    wl_resource* pResource = nullptr;

    {}DestroyWrapper resourceDestroyListener;

    void* pData = nullptr;)#",
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);
        } else {
            HEADER += R"#(
    wl_resource* pResource = nullptr;

    bool destroyed = false;

    void* pData = nullptr;)#";
        }

        HEADER += "\n};\n\n";
    }

    HEADER += "\n\n#undef F\n#undef wl_resource\n";
}

void parseSource() {
    SOURCE += std::format(R"#(#define private public
#define HYPRWAYLAND_SCANNER_NO_INTERFACES
#include "{}.hpp"
#undef private
#define F std::function
)#",
                          PROTO_DATA.fileName);

    // reference interfaces

    // dummy
    SOURCE += R"#(
static const wl_interface* dummyTypes[] = { nullptr };
)#";

    SOURCE += R"#(
// Reference all other interfaces.
// The reason why this is in snake is to
// be able to cooperate with existing
// wayland_scanner interfaces (they are interop)
)#";

    std::vector<std::string> declaredIfaces;

    for (auto& iface : XMLDATA.ifaces) {
        const auto IFACE_WL_NAME       = iface.name + "_interface";
        const auto IFACE_WL_NAME_CAMEL = camelize(iface.name + "_interface");
        declaredIfaces.push_back(IFACE_WL_NAME);

        SOURCE += std::format("extern const wl_interface {};\n", IFACE_WL_NAME, IFACE_WL_NAME_CAMEL, IFACE_WL_NAME);
    }

    for (auto& iface : XMLDATA.ifaces) {
        // do all referenced too
        for (auto& rq : iface.requests) {
            for (auto& arg : rq.args) {
                if (arg.interface.empty())
                    continue;

                const auto IFACE_WL_NAME2       = arg.interface + "_interface";
                const auto IFACE_WL_NAME_CAMEL2 = camelize(arg.interface + "_interface");

                if (std::find(declaredIfaces.begin(), declaredIfaces.end(), IFACE_WL_NAME2) == declaredIfaces.end()) {
                    SOURCE += std::format("extern const wl_interface {};\n", IFACE_WL_NAME2, IFACE_WL_NAME_CAMEL2, IFACE_WL_NAME2);
                    declaredIfaces.push_back(IFACE_WL_NAME2);
                }
            }
        }

        for (auto& ev : iface.events) {
            for (auto& arg : ev.args) {
                if (arg.interface.empty())
                    continue;

                const auto IFACE_WL_NAME2       = arg.interface + "_interface";
                const auto IFACE_WL_NAME_CAMEL2 = camelize(arg.interface + "_interface");

                if (std::find(declaredIfaces.begin(), declaredIfaces.end(), IFACE_WL_NAME2) == declaredIfaces.end()) {
                    SOURCE += std::format("extern const wl_interface {};\n", IFACE_WL_NAME2, IFACE_WL_NAME_CAMEL2, IFACE_WL_NAME2);
                    declaredIfaces.push_back(IFACE_WL_NAME2);
                }
            }
        }
    }

    // declare ifaces

    for (auto& iface : XMLDATA.ifaces) {

        const auto IFACE_WL_NAME          = iface.name + "_interface";
        const auto IFACE_NAME             = iface.name;
        const auto IFACE_NAME_CAMEL       = camelize(iface.name);
        const auto IFACE_CLASS_NAME_CAMEL = camelize((clientCode ? "CC_" : "C_") + iface.name);

        // create handlers
        for (auto& rq : (clientCode ? iface.events : iface.requests)) {
            const auto  REQUEST_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name);

            std::string argsC = ", ";
            for (auto& arg : rq.args) {
                if (arg.newType)
                    continue;
                argsC += WPTypeToCType(arg, false) + " " + arg.name + ", ";
            }

            argsC.pop_back();
            argsC.pop_back();

            std::string argsN = ", ";
            for (auto& arg : rq.args) {
                argsN += arg.name + ", ";
            }

            if (!argsN.empty()) {
                argsN.pop_back();
                argsN.pop_back();
            }

            if (!clientCode) {
                SOURCE += std::format(R"#(
static void {}(wl_client* client, wl_resource* resource{}) {{
    const auto PO = ({}*)wl_resource_get_user_data(resource);
    if (PO && PO->requests.{})
        PO->requests.{}(PO{});
}}
)#",
                                      REQUEST_NAME, argsC, IFACE_CLASS_NAME_CAMEL, camelize(rq.name), camelize(rq.name), argsN);
            } else {
                SOURCE += std::format(R"#(
static void {}(void* data, void* resource{}) {{
    const auto PO = ({}*)data;
    if (PO && PO->requests.{})
        PO->requests.{}(PO{});
}}
)#",
                                      REQUEST_NAME, argsC, IFACE_CLASS_NAME_CAMEL, camelize(rq.name), camelize(rq.name), argsN);
            }
        }

        // destroy handler
        if (!clientCode) {
            SOURCE += std::format(R"#(
static void _{}__DestroyListener(wl_listener* l, void* d) {{
    {}DestroyWrapper *wrap = wl_container_of(l, wrap, listener);
    {}* pResource = wrap->parent;
    pResource->onDestroyCalled();
}}
)#",
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);
        }

        // create vtable

        const auto IFACE_VTABLE_NAME = "_" + IFACE_CLASS_NAME_CAMEL + "VTable";

        SOURCE += std::format(R"#(
static const void* {}[] = {{
)#",
                              IFACE_VTABLE_NAME);

        for (auto& rq : (clientCode ? iface.events : iface.requests)) {
            const auto REQUEST_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name);
            SOURCE += std::format("    (void*){},\n", REQUEST_NAME);
        }

        SOURCE += "};\n";

        // create events

        int evid = 0;
        for (auto& ev : (!clientCode ? iface.events : iface.requests)) {
            const auto  EVENT_NAME = camelize("send_" + ev.name);

            std::string argsC = "";
            for (auto& arg : ev.args) {
                if (arg.newType)
                    continue;
                argsC += WPTypeToCType(arg, true) + " " + arg.name + ", ";
            }

            if (!argsC.empty()) {
                argsC.pop_back();
                argsC.pop_back();
            }

            std::string argsN = ", ";
            for (auto& arg : ev.args) {
                if (arg.newType)
                    argsN += "nullptr, ";
                else if (!WPTypeToCType(arg, true).starts_with("C"))
                    argsN += arg.name + ", ";
                else
                    argsN += (arg.name + " ? " + arg.name + "->pResource : nullptr, ");
            }

            argsN.pop_back();
            argsN.pop_back();

            if (!clientCode) {
                SOURCE += std::format(R"#(
void {}::{}({}) {{
    if (!pResource)
        return;
    wl_resource_post_event(pResource, {}{});
}}
)#",
                                      IFACE_CLASS_NAME_CAMEL, EVENT_NAME, argsC, evid, argsN);
            } else {
                std::string retType    = ev.newIdType.empty() ? "void" : "wl_proxy";
                std::string ptrRetType = ev.newIdType.empty() ? "void" : "wl_proxy*";
                std::string flags      = ev.destructor ? "1" : "0";
                SOURCE += std::format(R"#(
{} {}::{}({}) {{
    if (!pResource)
        return{};{}

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, {}, {}, wl_proxy_get_version((wl_proxy*)pResource), {}{});{}
}}
)#",
                                      ptrRetType, IFACE_CLASS_NAME_CAMEL, EVENT_NAME, argsC, (ev.newIdType.empty() ? "" : " nullptr"),
                                      (ev.destructor ? "\n    destroyed = true;" : ""), evid, (ev.newIdType.empty() ? "nullptr" : "&" + ev.newIdType + "_interface"), flags, argsN,
                                      (ev.newIdType.empty() ? "\n    proxy;" : "\n\n    return proxy;"));
            }

            evid++;
        }

        // dangerous
        if (!clientCode) {
            evid = 0;
            for (auto& ev : iface.events) {
                const auto  EVENT_NAME = camelize("send_" + ev.name + "_raw");

                std::string argsC = "";
                for (auto& arg : ev.args) {
                    if (arg.newType)
                        continue;
                    argsC += WPTypeToCType(arg, true, true) + " " + arg.name + ", ";
                }

                if (!argsC.empty()) {
                    argsC.pop_back();
                    argsC.pop_back();
                }

                std::string argsN = ", ";
                for (auto& arg : ev.args) {
                    if (arg.newType)
                        continue;
                    argsN += arg.name + ", ";
                }

                argsN.pop_back();
                argsN.pop_back();

                SOURCE += std::format(R"#(
void {}::{}({}) {{
    if (!pResource)
        return;
    wl_resource_post_event(pResource, {}{});
}}
)#",
                                      IFACE_CLASS_NAME_CAMEL, EVENT_NAME, argsC, evid, argsN);

                evid++;
            }
        }

        // wayland interfaces and stuff

        // type tables
        for (auto& rq : iface.requests) {
            if (rq.args.empty())
                continue;

            const auto TYPE_TABLE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name + "_types");
            SOURCE += std::format("static const wl_interface* {}[] = {{\n", TYPE_TABLE_NAME);

            for (auto& arg : rq.args) {
                if (arg.interface.empty()) {
                    SOURCE += "    nullptr,\n";
                    continue;
                }

                SOURCE += std::format("    &{}_interface,\n", arg.interface);
            }

            SOURCE += "};\n";
        }
        for (auto& ev : iface.events) {
            if (ev.args.empty())
                continue;

            const auto TYPE_TABLE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + ev.name + "_types");
            SOURCE += std::format("static const wl_interface* {}[] = {{\n", TYPE_TABLE_NAME);

            for (auto& arg : ev.args) {
                if (arg.interface.empty()) {
                    SOURCE += "    nullptr,\n";
                    continue;
                }

                SOURCE += std::format("    &{}_interface,\n", arg.interface);
            }

            SOURCE += "};\n";
        }

        const auto MESSAGE_NAME_REQUESTS = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_requests");
        const auto MESSAGE_NAME_EVENTS   = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_events");

        // message
        if (!noInterfaces) {
            if (iface.requests.size() > 0) {
                SOURCE += std::format(R"#(
static const wl_message {}[] = {{
)#",
                                      MESSAGE_NAME_REQUESTS);
                for (auto& rq : iface.requests) {
                    // create type table
                    const auto TYPE_TABLE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name + "_types");

                    SOURCE += std::format("    {{ \"{}\", \"{}\", {}}},\n", rq.name, argsToShort(rq.args, rq.since), rq.args.empty() ? "dummyTypes + 0" : TYPE_TABLE_NAME + " + 0");
                }

                SOURCE += "};\n";
            }

            if (iface.events.size() > 0) {
                SOURCE += std::format(R"#(
static const wl_message {}[] = {{
)#",
                                      MESSAGE_NAME_EVENTS);
                for (auto& ev : iface.events) {
                    // create type table
                    const auto TYPE_TABLE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + ev.name + "_types");

                    SOURCE += std::format("    {{ \"{}\", \"{}\", {}}},\n", ev.name, argsToShort(ev.args, ev.since), ev.args.empty() ? "dummyTypes + 0" : TYPE_TABLE_NAME + " + 0");
                }

                SOURCE += "};\n";
            }

            // iface
            SOURCE += std::format(R"#(
const wl_interface {} = {{
    "{}", {},
    {}, {},
    {}, {},
}};
)#",
                                  IFACE_WL_NAME, iface.name, iface.version, iface.requests.size(), (iface.requests.size() > 0 ? MESSAGE_NAME_REQUESTS : "nullptr"),
                                  iface.events.size(), (iface.events.size() > 0 ? MESSAGE_NAME_EVENTS : "nullptr"));
        }

        // protocol body
        if (!clientCode) {
            SOURCE += std::format(R"#(
{}::{}(wl_client* client, uint32_t version, uint32_t id) {{
    pResource = wl_resource_create(client, &{}, version, id);

    if (!pResource)
        return;

    wl_resource_set_user_data(pResource, this);
    wl_list_init(&resourceDestroyListener.listener.link);
    resourceDestroyListener.listener.notify = _{}__DestroyListener;
    resourceDestroyListener.parent = this;
    wl_resource_add_destroy_listener(pResource, &resourceDestroyListener.listener);

    wl_resource_set_implementation(pResource, {}, this, nullptr);
}}

{}::~{}() {{
    wl_list_remove(&resourceDestroyListener.listener.link);
    wl_list_init(&resourceDestroyListener.listener.link);

    // if we still own the wayland resource,
    // it means we need to destroy it.
    if (pResource && wl_resource_get_user_data(pResource) == this) {{
        wl_resource_set_user_data(pResource, nullptr);
        wl_resource_destroy(pResource);
    }}
}}

void {}::onDestroyCalled() {{
    wl_resource_set_user_data(pResource, nullptr);
    wl_list_remove(&resourceDestroyListener.listener.link);
    wl_list_init(&resourceDestroyListener.listener.link);

    // set the resource to nullptr,
    // as it will be freed. If the consumer does not destroy this resource
    // in onDestroy here, we'd be doing a UAF in the ~dtor
    pResource = nullptr;

    if (onDestroy)
        onDestroy(this);
}}
)#",
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_NAME + "_interface", IFACE_CLASS_NAME_CAMEL, IFACE_VTABLE_NAME, IFACE_CLASS_NAME_CAMEL,
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);
        } else {
            std::string DTOR_FUNC = "";

            for (auto& rq : iface.requests) {
                if (!rq.destructor)
                    continue;

                DTOR_FUNC = camelize("send_" + rq.name) + "()";
                break;
            }

            if (DTOR_FUNC.empty())
                DTOR_FUNC = "wl_proxy_destroy(pResource)";

            SOURCE += std::format(R"#(
{}::{}(wl_resource* resource) {{
    pResource = resource;

    if (!pResource)
        return;

    wl_proxy_add_listener(pResource, (void (**)(void))&{}, this);
}}

{}::~{}() {{
    if (!destroyed)
        {};
}}
)#",
                                  IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_VTABLE_NAME, IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, DTOR_FUNC);
        }

        for (auto& rq : (clientCode ? iface.events : iface.requests)) {
            std::string args = ", ";
            for (auto& arg : rq.args) {
                args += WPTypeToCType(arg, false) + ", ";
            }

            args.pop_back();
            args.pop_back();

            SOURCE += std::format(R"#(
void {}::{}(F<void({}*{})> handler) {{
    requests.{} = handler;
}}
)#",
                                  IFACE_CLASS_NAME_CAMEL, camelize("set_" + rq.name), IFACE_CLASS_NAME_CAMEL, args, camelize(rq.name));
        }
    }

    SOURCE += "\n#undef F\n";
}

int main(int argc, char** argv, char** envp) {
    std::string outpath   = "";
    std::string protopath = "";

    int         pathsTaken = 0;

    for (int i = 1; i < argc; ++i) {
        std::string curarg = argv[i];

        if (curarg == "-v" || curarg == "--version") {
            std::cout << SCANNER_VERSION << "\n";
            return 0;
        }

        if (curarg == "-c" || curarg == "--client") {
            clientCode = true;
            continue;
        }

        if (curarg == "--no-interfaces") {
            noInterfaces = true;
            continue;
        }

        if (curarg == "--wayland-enums") {
            waylandEnums = true;
            continue;
        }

        if (pathsTaken == 0) {
            protopath = curarg;
            pathsTaken++;
            continue;
        } else if (pathsTaken == 1) {
            outpath = curarg;
            pathsTaken++;
            continue;
        }

        std::cout << "Too many args or unknown arg " << curarg << "\n";
        return 1;
    }

    if (outpath.empty() || protopath.empty()) {
        std::cerr << "Not enough args\n";
        return 1;
    }

    // build!

    pugi::xml_document doc;
    if (!doc.load_file(protopath.c_str())) {
        std::cerr << "Couldn't load proto\n";
        return 1;
    }

    PROTO_DATA.nameOriginal = doc.child("protocol").attribute("name").as_string();
    PROTO_DATA.name         = camelize(PROTO_DATA.nameOriginal);
    PROTO_DATA.fileName     = protopath.substr(protopath.find_last_of('/') + 1, protopath.length() - (protopath.find_last_of('/') + 1) - 4);

    const auto COPYRIGHT =
        std::format("// Generated with hyprwayland-scanner {}. Made with vaxry's keyboard and ❤️.\n// {}\n\n/*\n This protocol's authors' copyright notice is:\n\n{}\n*/\n\n",
                    SCANNER_VERSION, PROTO_DATA.nameOriginal, std::string{doc.child("protocol").child("copyright").child_value()});

    parseXML(doc);
    parseHeader();
    parseSource();

    const auto HPATH              = outpath + "/" + PROTO_DATA.fileName + ".hpp";
    const auto CPATH              = outpath + "/" + PROTO_DATA.fileName + ".cpp";
    bool       needsToWriteHeader = true, needsToWriteSource = true;

    if (std::filesystem::exists(HPATH)) {
        // check if we need to overwrite

        std::ifstream headerIn(HPATH);
        std::string   content((std::istreambuf_iterator<char>(headerIn)), (std::istreambuf_iterator<char>()));

        if (content == COPYRIGHT + HEADER)
            needsToWriteHeader = false;

        headerIn.close();
    }

    if (std::filesystem::exists(CPATH)) {
        // check if we need to overwrite

        std::ifstream sourceIn(CPATH);
        std::string   content((std::istreambuf_iterator<char>(sourceIn)), (std::istreambuf_iterator<char>()));

        if (content == COPYRIGHT + SOURCE)
            needsToWriteSource = false;

        sourceIn.close();
    }

    if (needsToWriteHeader) {
        std::ofstream header(HPATH, std::ios::trunc);
        header << COPYRIGHT << HEADER;
        header.close();
    }

    if (needsToWriteSource) {
        std::ofstream source(CPATH, std::ios::trunc);
        source << COPYRIGHT << SOURCE;
        source.close();
    }

    return 0;
}