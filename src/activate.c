
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include "xdg-activation-v1-client-protocol.h"

/* DBus */

#define DBUS_BUS_NAME       "com.raspberrypi.prefapps"
#define DBUS_OBJECT_PATH    "/com/raspberrypi/prefapps"
#define DBUS_INTERFACE_NAME "com.raspberrypi.prefapps"

static guint busid;
static GDBusNodeInfo *introspection_data = NULL;

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='" DBUS_INTERFACE_NAME "'>"
  "    <method name='activate'>"
  "    </method>"
  "  </interface>"
  "</node>";

struct xdg_activation_v1 *activation;
struct wl_seat *wseat;
uint32_t last_serial;

GtkWidget *toact;

static void name_acquired (GDBusConnection *connection, const gchar *name, gpointer);
static void name_lost (GDBusConnection *connection, const gchar *name, gpointer);
static void handle_method_call (GDBusConnection *, const gchar*, const gchar*, const gchar*,
    const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer);
static void add_event_listeners (void);
static void activate_app (void);

/*----------------------------------------------------------------------------*/
/* DBus interface                                                             */
/*----------------------------------------------------------------------------*/

static const GDBusInterfaceVTable interface_vtable =
{
    handle_method_call, NULL, NULL, { 0 }
};

void init_dbus (void)
{
    busid = g_bus_own_name (G_BUS_TYPE_SESSION, DBUS_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        NULL, name_acquired, name_lost, NULL, NULL);
}

void close_dbus (void)
{
    g_bus_unown_name (busid);
}

static void name_acquired (GDBusConnection *connection, const gchar *name, gpointer)
{
    /* name not on DBus, so this is the first instance - set up handler for newtab function */
    introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    g_dbus_connection_register_object (connection, DBUS_OBJECT_PATH, introspection_data->interfaces[0],
        &interface_vtable, NULL, NULL, NULL);
}

static void name_lost (GDBusConnection *connection, const gchar *name, gpointer)
{
    GDBusProxy *proxy;

    /* name already on DBus, so application already running - call the newtab function on the existing instance and then exit */
    proxy = g_dbus_proxy_new_sync (connection, G_DBUS_PROXY_FLAGS_NONE, NULL, DBUS_BUS_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, NULL, NULL);
    g_dbus_proxy_call_sync (proxy, "activate", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_dbus_connection_close_sync (connection, NULL, NULL);

    g_object_unref (proxy);
    exit (0);
}

static void handle_method_call (GDBusConnection *, const gchar*, const gchar*, const gchar*,
    const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer)
{
    if (g_strcmp0 (method_name, "activate") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, NULL);
        if (getenv ("WAYLAND_DISPLAY")) activate_app ();
        else gtk_window_present (GTK_WINDOW (toact));
    }
    else g_dbus_method_invocation_return_dbus_error (invocation, DBUS_INTERFACE_NAME ".Failed", "Unsupported method call");
}

/*----------------------------------------------------------------------------*/
/* Wayland activation protocol */
/*----------------------------------------------------------------------------*/

static void token_handle_done (void *data, struct xdg_activation_token_v1 *token, const char *token_string)
{
    GdkWindow *win = gtk_widget_get_window (toact);
    struct wl_surface *surface = gdk_wayland_window_get_wl_surface (win);
    xdg_activation_v1_activate (activation, token_string, surface);
    xdg_activation_token_v1_destroy (token);
}

static const struct xdg_activation_token_v1_listener token_listener =
{
    .done = token_handle_done,
};

void activate_app (void)
{
    struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token (activation);
    xdg_activation_token_v1_add_listener (token, &token_listener, NULL);
    xdg_activation_token_v1_set_app_id (token, "rp-prefapps");
    xdg_activation_token_v1_set_serial (token, last_serial, wseat);
    xdg_activation_token_v1_commit (token);
}

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!g_strcmp0 (interface, xdg_activation_v1_interface.name))
    {
        activation = wl_registry_bind (registry, name, &xdg_activation_v1_interface, 1);
    }
}

static void registry_remove_object (void *, struct wl_registry *, uint32_t)
{
    xdg_activation_v1_destroy (activation);
}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

static void pointer_enter (void *, struct wl_pointer *, uint32_t serial, struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy)
{
    last_serial = serial;
}

static void pointer_button (void *, struct wl_pointer *, uint32_t serial, uint32_t, uint32_t, uint32_t)
{
    last_serial = serial;
}

static void pointer_leave (void *, struct wl_pointer *, uint32_t serial, struct wl_surface *surf)
{
    last_serial = serial;
}

static void pointer_motion (void *, struct wl_pointer *, uint32_t serial, wl_fixed_t, wl_fixed_t)
{
    last_serial = serial;
}

static void pointer_axis (void *, struct wl_pointer *, uint32_t serial, uint32_t, wl_fixed_t)
{
    last_serial = serial;
}

static void pointer_frame (void *, struct wl_pointer *) {}

static void pointer_axis_discrete (void *, struct wl_pointer *, uint32_t, int32_t) {}

static void pointer_axis_source (void *, struct wl_pointer *, uint32_t) {}

static void pointer_axis_stop (void *, struct wl_pointer *, uint32_t, uint32_t) {}

static const struct wl_pointer_listener pointer_listener =
{
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_discrete = pointer_axis_discrete,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
};

static void keyboard_keymap (void *, struct wl_keyboard *, uint32_t, int, uint32_t) {}

static void keyboard_enter (void *, struct wl_keyboard *, uint32_t serial, struct wl_surface *, struct wl_array *)
{
    last_serial = serial;
}

static void keyboard_leave (void *, struct wl_keyboard *, uint32_t serial, struct wl_surface *)
{
    last_serial = serial;
}

static void keyboard_key (void *, struct wl_keyboard *, uint32_t serial, uint32_t, uint32_t, uint32_t)
{
    last_serial = serial;
}

static void keyboard_modifiers (void *, struct wl_keyboard *, uint32_t serial, uint32_t, uint32_t, uint32_t, uint32_t)
{
    last_serial = serial;
}

static void keyboard_repeat_info (void *, struct wl_keyboard *, int32_t, int32_t) {}

static const struct wl_keyboard_listener keyboard_listener =
{
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void add_event_listeners (void)
{
    wl_pointer_add_listener (wl_seat_get_pointer (wseat), &pointer_listener, NULL);
    wl_keyboard_add_listener (wl_seat_get_keyboard (wseat), &keyboard_listener, NULL);
}

void setup_activate (GtkWidget *window)
{
    toact = window;
    
    if (getenv ("WAYLAND_DISPLAY"))
    {
        GdkDisplay *gdk_display = gdk_display_get_default ();
        GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
        struct wl_display *display = gdk_wayland_display_get_wl_display (gdk_display);
        struct wl_registry *registry = wl_display_get_registry (display);
        wseat  = gdk_wayland_seat_get_wl_seat (seat);

        wl_registry_add_listener (registry, &registry_listener, NULL);
        wl_display_roundtrip (display);
        wl_registry_destroy (registry);
        
        add_event_listeners ();
    }
}
