#include <pugixml.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <format>
#include <vector>
#include <algorithm>

struct SRequestArgument {
    std::string CType;
    std::string wlType;
    std::string interface;
    std::string name;
};

struct SRequest {
    std::vector<SRequestArgument> args;
    std::string                   name;
};

struct SEvent {
    std::vector<SRequestArgument> args;
    std::string                   name;
};

struct SInterface {
    std::vector<SRequest> requests;
    std::vector<SEvent>   events;
    std::string           name;
    int                   version = 1;
};

struct {
    std::vector<SInterface> ifaces;
} XMLDATA;

std::string argsToShort(std::vector<SRequestArgument>& args) {
    std::string shortt = "";
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
            shortt += "s";
        else if (a.wlType == "object")
            shortt += "o";
        else if (a.wlType == "array")
            shortt += "a";
        else if (a.wlType == "fd")
            shortt += "h";
        else
            throw std::runtime_error("Unknown arg in argsToShort");
    }
    return shortt;
}

std::string WPTypeToCType(const std::string& wptype) {
    if (wptype == "uint" || wptype == "new_id")
        return "uint32_t";
    if (wptype == "object")
        return "wl_resource*";
    if (wptype == "int" || wptype == "fd")
        return "int32_t";
    if (wptype == "fixed")
        return "wl_fixed_t";
    if (wptype == "array")
        return "wl_array*";
    if (wptype == "string")
        return "const char*";
    throw std::runtime_error("unknown wp type");
    return "";
}

std::string HEADER;
std::string SOURCE;

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

struct {
    std::string name;
    std::string nameOriginal;
    std::string fileName;
} PROTO_DATA;

void parseXML(pugi::xml_document& doc) {
    for (auto& iface : doc.child("protocol").children("interface")) {
        SInterface ifc;
        ifc.name    = iface.attribute("name").as_string();
        ifc.version = iface.attribute("version").as_int();

        for (auto& rq : iface.children("request")) {
            SRequest srq;
            srq.name = rq.attribute("name").as_string();

            for (auto& arg : rq.children("arg")) {
                SRequestArgument sargm;
                sargm.name      = arg.attribute("name").as_string();
                sargm.CType     = WPTypeToCType(arg.attribute("type").as_string());
                sargm.wlType    = arg.attribute("type").as_string();
                sargm.interface = arg.attribute("interface").as_string();

                srq.args.push_back(sargm);
            }

            ifc.requests.push_back(srq);
        }

        for (auto& ev : iface.children("event")) {
            SEvent sev;
            sev.name = ev.attribute("name").as_string();

            for (auto& arg : ev.children("arg")) {
                SRequestArgument sargm;
                sargm.name      = arg.attribute("name").as_string();
                sargm.CType     = WPTypeToCType(arg.attribute("type").as_string());
                sargm.interface = arg.attribute("interface").as_string();
                sargm.wlType    = arg.attribute("type").as_string();

                sev.args.push_back(sargm);
            }

            ifc.events.push_back(sev);
        }

        XMLDATA.ifaces.push_back(ifc);
    }
}

void parseHeader() {

    // add some boilerplate
    HEADER += R"#(#pragma once

#include <functional>
#include <cstdint>
#include <wayland-server.h>

#define F std::function

struct wl_client;
struct wl_resource;

)#";

    for (auto& iface : XMLDATA.ifaces) {
        const auto IFACE_NAME_CAMEL       = camelize(iface.name);
        const auto IFACE_CLASS_NAME_CAMEL = camelize("C_" + iface.name);

        // begin the class
        HEADER += std::format(R"#(
class {} {{
  public:
    {}(wl_client* client, uint32_t version, uint32_t id);
    ~{}();

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

)#",
                              IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);

        // add all setters for requests
        HEADER += "\n    // --------------- Requests --------------- //\n\n";

        for (auto& rq : iface.requests) {

            std::string args = ", ";
            for (auto& arg : rq.args) {
                args += arg.CType + ", ";
            }

            args.pop_back();
            args.pop_back();

            HEADER += std::format("    void {}(F<void(wl_client*, wl_resource*{})> handler);\n", camelize("set_" + rq.name), args);
        }

        // start events

        HEADER += "\n    // --------------- Events --------------- //\n\n";

        for (auto& ev : iface.events) {
            std::string args = "";
            for (auto& arg : ev.args) {
                args += arg.CType + ", ";
            }

            if (!args.empty()) {
                args.pop_back();
                args.pop_back();
            }

            HEADER += std::format("    void {}({});\n", camelize("send_" + ev.name), args);
        }

        // end events

        // start private section
        HEADER += "\n  private:\n";

        // start requests storage
        HEADER += "    struct {\n";

        for (auto& rq : iface.requests) {

            std::string args = ", ";
            for (auto& arg : rq.args) {
                args += arg.CType + ", ";
            }

            args.pop_back();
            args.pop_back();

            HEADER += std::format("        F<void(wl_client*, wl_resource*{})> {};\n", args, camelize(rq.name));
        }

        // end requests storage
        HEADER += "    } requests;\n";

        // constant resource stuff
        HEADER += std::format(R"#(
    void onDestroyCalled();

    F<void({}*)> onDestroy;

    wl_resource* pResource = nullptr;

    wl_listener resourceDestroyListener;

    void* pData = nullptr;)#",
                              IFACE_CLASS_NAME_CAMEL);

        HEADER += "\n};\n\n";
    }

    HEADER += "\n\n#undef F\n";
}

void parseSource() {
    SOURCE += std::format(R"#(#define private public
#include "{}.hpp"
#undef private
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

        if (std::find(declaredIfaces.begin(), declaredIfaces.end(), IFACE_WL_NAME) == declaredIfaces.end()) {
            SOURCE += std::format("extern const wl_interface {};\n#define {} {}\n", IFACE_WL_NAME, IFACE_WL_NAME_CAMEL, IFACE_WL_NAME);

            declaredIfaces.push_back(IFACE_WL_NAME);
        }

        // do all referenced too
        for (auto& rq : iface.requests) {
            for (auto& arg : rq.args) {
                if (arg.interface.empty())
                    continue;

                const auto IFACE_WL_NAME2       = arg.interface + "_interface";
                const auto IFACE_WL_NAME_CAMEL2 = camelize(arg.interface + "_interface");

                if (std::find(declaredIfaces.begin(), declaredIfaces.end(), IFACE_WL_NAME2) == declaredIfaces.end()) {
                    SOURCE += std::format("extern const wl_interface {};\n#define {} {}\n", IFACE_WL_NAME2, IFACE_WL_NAME_CAMEL2, IFACE_WL_NAME2);
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
                    SOURCE += std::format("extern const wl_interface {};\n#define {} {}\n", IFACE_WL_NAME2, IFACE_WL_NAME_CAMEL2, IFACE_WL_NAME2);
                    declaredIfaces.push_back(IFACE_WL_NAME2);
                }
            }
        }
    }

    // declare ifaces

    for (auto& iface : XMLDATA.ifaces) {

        const auto IFACE_NAME             = iface.name;
        const auto IFACE_NAME_CAMEL       = camelize(iface.name);
        const auto IFACE_CLASS_NAME_CAMEL = camelize("C_" + iface.name);

        // create handlers
        for (auto& rq : iface.requests) {
            const auto  REQUEST_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name);

            std::string argsC = ", ";
            for (auto& arg : rq.args) {
                argsC += arg.CType + " " + arg.name + ", ";
            }

            argsC.pop_back();
            argsC.pop_back();

            std::string argsN = ", ";
            for (auto& arg : rq.args) {
                argsN += arg.name + ", ";
            }

            argsN.pop_back();
            argsN.pop_back();

            SOURCE += std::format(R"#(
static void {}(wl_client* client, wl_resource* resource{}) {{
    const auto PO = ({}*)wl_resource_get_user_data(resource);
    if (PO->requests.{})
        PO->requests.{}(client, resource{});
}}
)#",
                                  REQUEST_NAME, argsC, IFACE_CLASS_NAME_CAMEL, camelize(rq.name), camelize(rq.name), argsN);
        }

        // destroy handler
        SOURCE += std::format(R"#(
static void _{}__DestroyListener(wl_listener* l, void* d) {{
    {}* pResource = wl_container_of(l, pResource, resourceDestroyListener);
    pResource->onDestroyCalled();
}}
)#",
                              IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);

        // create vtable

        const auto IFACE_VTABLE_NAME = "_" + IFACE_CLASS_NAME_CAMEL + "VTable";

        SOURCE += std::format(R"#(
static const void* {}[] = {{
)#",
                              IFACE_VTABLE_NAME);

        for (auto& rq : iface.requests) {
            const auto REQUEST_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name);
            SOURCE += std::format("    {},\n", REQUEST_NAME);
        }

        SOURCE += "};\n";

        // create events

        int evid = 0;
        for (auto& ev : iface.events) {
            const auto  EVENT_NAME = camelize("send_" + ev.name);

            std::string argsC = ", ";
            for (auto& arg : ev.args) {
                argsC += arg.CType + " " + arg.name + ", ";
            }

            argsC.pop_back();
            argsC.pop_back();

            std::string argsN = ", ";
            for (auto& arg : ev.args) {
                argsN += arg.name + ", ";
            }

            argsN.pop_back();
            argsN.pop_back();

            SOURCE += std::format(R"#(
void {}::{}({}) {{
    wl_resource_post_event(pResource, {}{});
}}
)#",
                                  IFACE_CLASS_NAME_CAMEL, EVENT_NAME, argsC, evid, argsN);

            evid++;
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

        const auto MESSAGE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_message");

        // message
        SOURCE += std::format(R"#(
static const wl_message {}[] = {{
)#",
                              MESSAGE_NAME);
        for (auto& rq : iface.requests) {
            // create type table
            const auto TYPE_TABLE_NAME = camelize(std::string{"_"} + "C_" + IFACE_NAME + "_" + rq.name + "_types");

            SOURCE += std::format("    {{ \"{}\", \"{}\", {}}},\n", rq.name, argsToShort(rq.args), rq.args.empty() ? "dummyTypes + 0" : TYPE_TABLE_NAME + " + 0");
        }

        SOURCE += "};\n";

        // protocol body
        SOURCE += std::format(R"#(
{}::{}(wl_client* client, uint32_t version, uint32_t id) {{
    pResource = wl_resource_create(client, &{}, version, id);

    if (!pResource)
        return;

    wl_resource_set_user_data(pResource, this);
    wl_list_init(&resourceDestroyListener.link);
    resourceDestroyListener.notify = _{}__DestroyListener;
    wl_resource_add_destroy_listener(pResource, &resourceDestroyListener);

    wl_resource_set_implementation(pResource, {}, this, nullptr);
}}

{}::~{}() {{
    wl_list_remove(&resourceDestroyListener.link);
    wl_list_init(&resourceDestroyListener.link);

    // if we still own the wayland resource,
    // it means we need to destroy it.
    if (wl_resource_get_user_data(pResource) == this)
        wl_resource_destroy(pResource);
}}

void {}::onDestroyCalled() {{
    wl_resource_set_user_data(pResource, nullptr);

    if (onDestroy)
        onDestroy(this);
}}
)#",
                              IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL, IFACE_NAME + "_interface", IFACE_CLASS_NAME_CAMEL, IFACE_VTABLE_NAME, IFACE_CLASS_NAME_CAMEL,
                              IFACE_CLASS_NAME_CAMEL, IFACE_CLASS_NAME_CAMEL);
    }
}

int main(int argc, char** argv, char** envp) {
    std::string outpath   = "";
    std::string protopath = "";

    for (int i = 1; i < argc; ++i) {
        std::string curarg = argv[i];
        if (i == 1)
            protopath = curarg;
        else if (i == 2)
            outpath = curarg;
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

    std::ofstream header(outpath + "/" + PROTO_DATA.fileName + ".hpp", std::ios::trunc);
    header << COPYRIGHT << HEADER;
    header.close();
    std::ofstream source(outpath + "/" + PROTO_DATA.fileName + ".cpp", std::ios::trunc);
    source << COPYRIGHT << SOURCE;
    source.close();

    return 0;
}