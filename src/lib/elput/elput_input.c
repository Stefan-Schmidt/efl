#include "elput_private.h"

static int
_cb_open_restricted(const char *path, int flags, void *data)
{
   Elput_Manager *em;

   em = data;
   return elput_manager_open(em, path, flags);
}

static void
_cb_close_restricted(int fd, void *data)
{
   Elput_Manager *em;

   em = data;
   elput_manager_close(em, fd);
}

const struct libinput_interface _input_interface =
{
   _cb_open_restricted,
   _cb_close_restricted,
};

static Elput_Seat *
_udev_seat_create(Elput_Manager *em, const char *name)
{
   Elput_Seat *eseat;

   eseat = calloc(1, sizeof(Elput_Seat));
   if (!eseat) return NULL;

   eseat->name = eina_stringshare_add(name);
   em->input.seats = eina_list_append(em->input.seats, eseat);

   return eseat;
}

static void
_udev_seat_destroy(Elput_Seat *eseat)
{
   Elput_Device *edev;

   EINA_LIST_FREE(eseat->devices, edev)
     _evdev_device_destroy(edev);

   if (eseat->kbd) _evdev_keyboard_destroy(eseat->kbd);
   if (eseat->ptr) _evdev_pointer_destroy(eseat->ptr);
   if (eseat->touch) _evdev_touch_destroy(eseat->touch);

   eina_stringshare_del(eseat->name);
   free(eseat);
}

static Elput_Seat *
_udev_seat_named_get(Elput_Manager *em, const char *name)
{
   Elput_Seat *eseat;
   Eina_List *l;

   EINA_LIST_FOREACH(em->input.seats, l, eseat)
     if (!strcmp(eseat->name, name)) return eseat;

   return _udev_seat_create(em, name);
}

static Elput_Seat *
_udev_seat_get(Elput_Manager *em, struct libinput_device *device)
{
   struct libinput_seat *lseat;
   const char *name;

   lseat = libinput_device_get_seat(device);
   name = libinput_seat_get_logical_name(lseat);

   return _udev_seat_named_get(em, name);
}

static void
_device_add(Elput_Manager *em, struct libinput_device *dev)
{
   Elput_Seat *eseat;
   Elput_Device *edev;
   const char *oname;

   eseat = _udev_seat_get(em, dev);
   if (!eseat) return;

   edev = _evdev_device_create(eseat, dev);
   if (!edev) return;

   oname = libinput_device_get_output_name(dev);
   eina_stringshare_replace(&edev->output_name, oname);

   eseat->devices = eina_list_append(eseat->devices, edev);
}

static void
_device_remove(Elput_Manager *em, struct libinput_device *device)
{
   Elput_Seat *eseat;
   Elput_Device *edev;

   edev = libinput_device_get_user_data(device);
   if (!edev) return;

   eseat = _udev_seat_get(em, device);
   if (eseat)
     eseat->devices = eina_list_remove(eseat->devices, edev);

   _evdev_device_destroy(edev);
}

static int
_udev_process_event(struct libinput_event *event)
{
   Elput_Manager *em;
   struct libinput *lib;
   struct libinput_device *dev;
   int ret = 1;

   lib = libinput_event_get_context(event);
   dev = libinput_event_get_device(event);
   em = libinput_get_user_data(lib);

   switch (libinput_event_get_type(event))
     {
      case LIBINPUT_EVENT_DEVICE_ADDED:
        DBG("Input Device Added: %s", libinput_device_get_name(dev));
        _device_add(em, dev);
        break;
      case LIBINPUT_EVENT_DEVICE_REMOVED:
        DBG("Input Device Removed: %s", libinput_device_get_name(dev));
        _device_remove(em, dev);
        break;
      default:
        ret = 0;
        break;
     }

   return ret;
}

static void
_process_event(struct libinput_event *event)
{
   if (_udev_process_event(event)) return;
   if (_evdev_event_process(event)) return;
}

static void
_process_events(Elput_Input *ei)
{
   struct libinput_event *event;

   while ((event = libinput_get_event(ei->lib)))
     {
        _process_event(event);
        libinput_event_destroy(event);
     }
}

static Eina_Bool
_cb_input_dispatch(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   Elput_Input *ei;

   ei = data;

   if (libinput_dispatch(ei->lib) != 0)
     WRN("libinput failed to dispatch events");

   _process_events(ei);

   return EINA_TRUE;
}

EAPI Eina_Bool
elput_input_init(Elput_Manager *manager, const char *seat)
{
   int fd;

   EINA_SAFETY_ON_NULL_RETURN_VAL(manager, EINA_FALSE);

   memset(&manager->input, 0, sizeof(Elput_Input));

   manager->input.lib =
     libinput_udev_create_context(&_input_interface, manager, eeze_udev_get());
   if (!manager->input.lib)
     {
        ERR("libinput could not create udev context");
        goto udev_err;
     }

   if (libinput_udev_assign_seat(manager->input.lib, seat) != 0)
     {
        ERR("libinput could not assign udev seat");
        goto seat_err;
     }

   _process_events(&manager->input);

   fd = libinput_get_fd(manager->input.lib);

   manager->input.hdlr =
     ecore_main_fd_handler_add(fd, ECORE_FD_READ, _cb_input_dispatch,
                               &manager->input, NULL, NULL);
   if (!manager->input.hdlr)
     {
        ERR("Could not create input fd handler");
        goto hdlr_err;
     }

   return EINA_TRUE;

hdlr_err:
seat_err:
   libinput_unref(manager->input.lib);
udev_err:
   return EINA_FALSE;
}

EAPI void
elput_input_shutdown(Elput_Manager *manager)
{
   Elput_Seat *seat;

   EINA_SAFETY_ON_NULL_RETURN(manager);
   EINA_SAFETY_ON_NULL_RETURN(&manager->input);

   if (manager->input.hdlr) ecore_main_fd_handler_del(manager->input.hdlr);

   EINA_LIST_FREE(manager->input.seats, seat)
     _udev_seat_destroy(seat);

   libinput_unref(manager->input.lib);
}
