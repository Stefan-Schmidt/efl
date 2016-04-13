#ifndef _ELPUT_H
# define _ELPUT_H

# ifdef EAPI
#  undef EAPI
# endif

# ifdef _MSC_VER
#  ifdef BUILDING_DLL
#   define EAPI __declspec(dllexport)
#  else // ifdef BUILDING_DLL
#   define EAPI __declspec(dllimport)
#  endif // ifdef BUILDING_DLL
# else // ifdef _MSC_VER
#  ifdef __GNUC__
#   if __GNUC__ >= 4
#    define EAPI __attribute__ ((visibility("default")))
#   else // if __GNUC__ >= 4
#    define EAPI
#   endif // if __GNUC__ >= 4
#  else // ifdef __GNUC__
#   define EAPI
#  endif // ifdef __GNUC__
# endif // ifdef _MSC_VER

# ifdef EFL_BETA_API_SUPPORT

/* opaque structure to represent an input manager */
typedef struct _Elput_Manager Elput_Manager;

/* opaque structure to represent an input seat */
typedef struct _Elput_Seat Elput_Seat;

/* opaque structure to represent an input device */
typedef struct _Elput_Device Elput_Device;

/* opaque structure to represent a keyboard */
typedef struct _Elput_Keyboard Elput_Keyboard;

/* opaque structure to represent a mouse */
typedef struct _Elput_Pointer Elput_Pointer;

/* opaque structure to represent a touch device */
typedef struct _Elput_Touch Elput_Touch;

/* structure to represent event for seat capability changes */
typedef struct _Elput_Event_Seat_Caps
{
   int pointer_count;
   int keyboard_count;
   int touch_count;
   Elput_Seat *seat;
} Elput_Event_Seat_Caps;

/* structure to represent event for seat frame */
typedef struct _Elput_Event_Seat_Frame
{
   Elput_Seat *seat;
} Elput_Event_Seat_Frame;

/* structure to represent event for seat keymap changes */
typedef struct _Elput_Event_Keymap_Send
{
   int fd, format;
   size_t size;
} Elput_Event_Keymap_Send;

/* structure to represent event for seat modifiers changes */
typedef struct _Elput_Event_Modifiers_Send
{
   unsigned int depressed;
   unsigned int latched;
   unsigned int locked;
   unsigned int group;
} Elput_Event_Modifiers_Send;

EAPI extern int ELPUT_EVENT_SEAT_CAPS;
EAPI extern int ELPUT_EVENT_SEAT_FRAME;
EAPI extern int ELPUT_EVENT_KEYMAP_SEND;
EAPI extern int ELPUT_EVENT_MODIFIERS_SEND;

/**
 * @file
 * @brief Ecore functions for dealing with libinput
 *
 * @defgroup Elput_Group Elput - libinput integration
 * @ingrup Ecore
 *
 * Elput provides a wrapper and functions for using libinput
 *
 * @li @ref Elput_Init_Group
 * @li @ref Elput_Manager_Group
 * @li @ref Elput_Input_Group
 *
 */

/**
 * @defgroup Elput_Init_Group Library Init and Shutdown functions
 *
 * Functions that start and shutdown the Elput library
 */

/**
 * Initialize the Elput library
 *
 * @return The number of times the library has been initialized without being
 *         shutdown. 0 is returned if an error occurs.
 *
 * @ingroup Elput_Init_Group
 * @since 1.18
 */
EAPI int elput_init(void);

/**
 * Shutdown the Elput library
 *
 * @return The number of times the library has been initialized without being
 *         shutdown. 0 is returned if an error occurs.
 *
 * @ingroup Elput_Init_Group
 * @since 1.18
 */
EAPI int elput_shutdown(void);

/**
 * @defgroup Elput_Manager_Group
 *
 * Functions that deal with connecting, disconnecting, opening, closing
 * of input devices.
 */

/**
 * Create an input manager on the specified seat
 *
 * @param seat
 * @param tty
 * @param sync
 *
 * @return A Elput_Manager on success, NULL on failure
 *
 * @ingroup Elput_Manager_Group
 * @since 1.18
 */
EAPI Elput_Manager *elput_manager_connect(const char *seat, unsigned int tty, Eina_Bool sync);

/**
 * Disconnect an input manager
 *
 * @param manager
 *
 * @ingroup Elput_Manager_Group
 * @since 1.18
 */
EAPI void elput_manager_disconnect(Elput_Manager *manager);

/**
 * Request input manager to open a file
 *
 * @param manager
 * @param path
 * @param flags
 *
 * @return Filedescriptor of opened file or -1 on failure
 *
 * @ingroup Elput_Manager_Group
 * @since 1.18
 */
EAPI int elput_manager_open(Elput_Manager *manager, const char *path, int flags);

/**
 * Request input manager to close a file
 *
 * @param manager
 * @param fd
 *
 * @ingroup Elput_Manager_Group
 * @since 1.18
 */
EAPI void elput_manager_close(Elput_Manager *manager, int fd);

/**
 * @defgroup Elput_Input_Group Elput input functions
 *
 * Functions that deal with setup of inputs
 */

/**
 * Initialize input
 *
 * @param manager
 * @param seat
 *
 * @return EINA_TRUE on success, EINA_FALSE on failure
 *
 * @ingroup Elput_Input_Group
 * @since 1.18
 */
EAPI Eina_Bool elput_input_init(Elput_Manager *manager, const char *seat);

/**
 * Shutdown input
 *
 * @param manager
 *
 * @ingroup Elput_Input_Group
 * @since 1.18
 */
EAPI void elput_input_shutdown(Elput_Manager *manager);

# endif

# undef EAPI
# define EAPI

#endif
