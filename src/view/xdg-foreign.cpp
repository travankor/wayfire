#include "xdg-foreign.hpp"
#include "view.hpp"
#include "core.hpp"
#include <map>
#include <algorithm>

#define export Export
#include "xdg-foreign-unstable-v1-protocol.h"
#undef export

namespace wf
{

class wxdg_foreign_handle;
std::map<std::string, wxdg_foreign_handle*> handles;

class wxdg_foreign_child : public custom_data_t
{
  public:
    wl_resource *used_imported = NULL;
};

class wxdg_foreign_handle : public custom_data_t
{
    wl_resource *exported;
    std::vector<wl_resource*> imported;
    std::string handle_string;

  public:
    wayfire_view for_view;

    wxdg_foreign_handle(wayfire_view for_view,
        wl_resource *exported)
    {
        this->exported = exported;
        wl_resource_set_user_data(exported, this);

        /* FIXME: this is bad for X,Y,Z */
        static int ID = 0;
        this->handle_string = std::to_string(++ID);
        zxdg_exported_v1_send_handle(exported, handle_string.c_str());

        /* Register globally */
        handles[this->handle_string] = this;
    }

    /**
     * Register a new imported instance for the current exported handle.
     */
    void import(wl_resource *imported_resource)
    {
        this->imported.push_back(imported_resource);
        wl_resource_set_user_data(imported_resource, this);
    }

#define WXDG_CHILD(v) v->get_data_safe<wxdg_foreign_child>()

    void unimport(wl_resource *unimported_resource)
    {
        auto it = std::find(imported.begin(), imported.end(),
            unimported_resource);
        imported.erase(it);
        wl_resource_set_user_data(unimported_resource, NULL);

        /* Remove any children relations which were set using this importer */
        auto children = for_view->children;
        for (auto child : children)
        {
            if (WXDG_CHILD(child)->used_imported == unimported_resource)
            {
                child->set_toplevel_parent(nullptr);
                WXDG_CHILD(child)->used_imported = nullptr;
            }
        }
    }

    void add_child(wl_resource *used_imported, wayfire_view child)
    {
        child->set_toplevel_parent(this->for_view);
        WXDG_CHILD(child)->used_imported = used_imported;
    }

    ~wxdg_foreign_handle()
    {
        /* Unregister */
        handles.erase(this->handle_string);

        /* Make resources inert */
        wl_resource_set_user_data(exported, NULL);
        auto imp = imported;
        for (auto resource : imp)
            unimport(resource);
    }
};

void xdg_exported_handle_destroy(wl_client *client, wl_resource *resource)
{
    auto exp = (wxdg_foreign_handle*) wl_resource_get_user_data(resource);
    if (exp) exp->for_view->erase_data<wxdg_foreign_handle>();
}

const struct zxdg_exported_v1_interface exported_impl = {
    .destroy = xdg_exported_handle_destroy,
};

void xdg_imported_handle_destroy(wl_client *client, wl_resource *resource)
{
    auto exp = (wxdg_foreign_handle*) wl_resource_get_user_data(resource);
    if (exp) exp->unimport(resource);
}

void xdg_imported_handle_set_parent_of(wl_client *client,
    wl_resource *resource, wl_resource *surface)
{
    auto exp = (wxdg_foreign_handle*) wl_resource_get_user_data(resource);
    auto child = wl_surface_to_wayfire_view(surface);
    if (exp && child) {
        exp->add_child(resource, child);
    } else {
        wl_client_post_no_memory(client);
    }
}

const struct zxdg_imported_v1_interface imported_impl = {
    .destroy = xdg_imported_handle_destroy,
    .set_parent_of = xdg_imported_handle_set_parent_of,
};

void xdg_exporter_handle_destroy(wl_client *client, wl_resource *resource)
{ /* Nothing */ }

void xdg_exporter_handle_export(wl_client *client, wl_resource *resource,
    uint32_t id, wl_resource *surface)
{
    auto view = wl_surface_to_wayfire_view(surface);
    if (!view)
    {
        wl_client_post_no_memory(client);
        return;
    }

    auto exported_resource =
        wl_resource_create(client, &zxdg_exported_v1_interface, 1, id);
    wl_resource_set_implementation(exported_resource, &exported_impl, NULL, NULL);
    view->store_data(
        std::make_unique<wxdg_foreign_handle> (view, exported_resource));
}

const struct zxdg_exporter_v1_interface exporter_impl = {
    .destroy = xdg_exporter_handle_destroy,
    .Export = xdg_exporter_handle_export,
};

void xdg_importer_handle_destroy(wl_client *client, wl_resource *resource)
{ /* Nothing */ }

void xdg_importer_handle_import(wl_client *client, wl_resource *resource,
    uint32_t id, const char *handle)
{
    auto exp = handles.find(handle);
    if (exp != handles.end())
    {
        auto imported_resource =
            wl_resource_create(client, &zxdg_imported_v1_interface, 1, id);
        wl_resource_set_implementation(imported_resource, &imported_impl, NULL, NULL);
        exp->second->import(imported_resource);
    } else {
        wl_client_post_no_memory(client);
    }
}

const struct zxdg_importer_v1_interface importer_impl = {
    .destroy = xdg_importer_handle_destroy,
    .import = xdg_importer_handle_import,
};

void bind_xdg_foreign_exporter(wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto resource =
        wl_resource_create(client, &zxdg_exporter_v1_interface, 1, id);
    wl_resource_set_implementation(resource, &exporter_impl, NULL, NULL);
}

void bind_xdg_foreign_importer(wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto resource =
        wl_resource_create(client, &zxdg_importer_v1_interface, 1, id);
    wl_resource_set_implementation(resource, &importer_impl, NULL, NULL);
}

xdg_foreign_implementation_t::xdg_foreign_implementation_t(wl_display *display)
{
    wl_global_create(display, &zxdg_exporter_v1_interface,
        1, NULL, bind_xdg_foreign_exporter);
    wl_global_create(display, &zxdg_importer_v1_interface,
        1, NULL, bind_xdg_foreign_importer);
}
}
