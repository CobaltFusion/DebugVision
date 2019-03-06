/************************************************************************

    Description:
        Plugin interface definition for DebugVisualizer (NetxGen DebugView++)

        Status: DRAFT

    Author(s):
        Udo Eberhardt

************************************************************************/

#pragma once 

#ifndef __cplusplus
#error This interface definition requires C++.
#endif

#include <stdint.h>


/*
    Some notes on using C++ interfaces across DLL boundaries:
    
    Because the DebugVisualizer EXE and a plugin DLL can be built using different 
    compiler versions and different versions of the C++ runtime library,
    some binary compatibility issues need to be considered.
    The C++ standard does not (yet) define a binary interface standard (ABI).
    Therefore, for our interface definition, some restrictions apply as outlined below.

    - We can use abstract interfaces only (C++ classes that contain pure 
      virtual methods only, but no data members). This way we rely
      on the compiler's virtual function table layout. This is what 
      Microsoft COM technology does as well.

    - We can pass integral types (integers, pointers, references) as function
      arguments, and can pass structs (or unions) containing such members.
      But we cannot pass more complex objects such as STL containers.
    
    - If we exchange dynamically allocated objects (e.g. structs) between DLL and EXE,
      we need to ensure that the code which allocated the object also frees it.
      In other words, a dynamically allocated memory resource must be passed back to 
      the owner for freeing it.

    - Exceptions cannot cross interface boundaries because exception frame layout 
      depends on compiler version and settings and might not be compatible.
      We need to use return status values.

*/


// Calling convention for exported C-style functions.
#define DVP_FUNCTION_CALL  __stdcall


//
// Return status codes used at the interface level.
//
enum class DVP_Status : int32_t
{
    Success = 0,
    Failed,
    BufferTooSmall,
    NotSupported,
    NotPresent,
};



/*
    Plugin loading/unloading
    ------------------------

    Any plugin DLL is assumed to reside in a specific subdirectory of a given
    DebugVisualizer installation, for example:
      c:\Program Files\DebugVisualizer\plugins

    On startup, DebugVisualizer enumerates all .dll files in the plugins subdirectory,
    and performs the following steps with each DLL:

    1. DebugVisualizer loads the DLL using the Windows function LoadLibrary().

    2. DebugVisualizer tries to resolve the function names DVP_LoadPlugin 
       and DVP_UnloadPlugin using the Windows function GetProcAddress().

    3. DebugVisualizer calls DVP_LoadPlugin() and checks the return value.

    4. DebugVisualizer calls DVP_GetPluginInterface() passing the current interface version 
       that it supports, and checks the return value.
       If the function returns nullptr then (optionally) DebugVisualizer 
       retries the call passing an older version of the interface.

    5. DebugVisualizer calls DVP_SetHostInterface() passing the current interface version 
       that it implements, and checks the return value.

    If any of the above steps fails, DebugVisualizer skips the given DLL and continues
    with the next one.

    If all steps succeed, the plugin was successfully loaded and interface pointers 
    have been exchanged. While running, DebugVisualizer keeps track of the loaded plugin.
    When the application quits, DebugVisualizer calls DVP_UnloadPlugin() and then unloads
    the DLL using the Windows function FreeLibrary().


    Threading model
    ---------------
    
    DebugVisualizer guarantees that all calls to the following functions exported by
    a given plugin are serialized. These functions never execute concurrently.
    - DVP_LoadPlugin()
    - DVP_UnloadPlugin()
    - DVP_GetPluginInterface()
    - DVP_SetHostInterface()

*/

extern "C"
DVP_Status
DVP_FUNCTION_CALL
DVP_LoadPlugin();

extern "C"
void
DVP_FUNCTION_CALL
DVP_UnloadPlugin();


//
// Returns an interface pointer if the requested interface version is implemented by 
// the plugin. According to interfaceVersion, the return value can be casted to 
// dvplugin::PluginInterface* or a subclass.
//
// Returns nullptr if the requested interface version is not implemented by the 
// plugin. In this case, DebugVisualizer may retry the call with an older 
// interface version.
//
extern "C"
void*
DVP_FUNCTION_CALL
DVP_GetPluginInterface(
    uint32_t interfaceVersion
    );

//
// According to interfaceVersion, the interface pointer can be casted to 
// dvplugin::HostInterface* or a subclass.
//
// Returns DVP_Status::Success if the provided interface has been accepted
// by the plugin, an error status otherwise. 
//
// Plugin code can accept a newer version of the interface than it has been 
// compiled with because the new version is a subclass of the previous version.
//
extern "C"
DVP_Status
DVP_FUNCTION_CALL
DVP_SetHostInterface(
    uint32_t interfaceVersion,
    void* interface
    );



namespace dvplugin {


//
// Constants used at the interface level.
//
struct Constants
{
    static constexpr uint32_t MaxStringSize = 128;
    static constexpr uint32_t UniqueIdSize = 64;
};

//
// Information about the plugin.
//
struct PluginDescription
{
    // For information only.
    wchar_t name[Constants::MaxStringSize];
    wchar_t provider[Constants::MaxStringSize];

    // Unique identifier used to establish a configuration data context.
    wchar_t uniqueId[Constants::UniqueIdSize];

    // Version of the plugin implementation, not of the interface!
    uint32_t majorVersion;
    uint32_t minorVersion;
};


//
// Information about a custom metadata item implemented by the plugin.
//
struct MetadataDescription
{
    uint32_t id;
    const wchar_t* name;
};

//
// Metadata associated with a message is described by 
// a chain of MetadataItem objects.
//
struct MetadataItem
{
    // identifies the data item
    uint32_t id;
    
    // data is always a null-terminated string
    const wchar_t* data,

    // nullptr, or pointer to next item in chain
    const MetadataItem* next;
};

//
// Definition of ID values.
//
struct MetadataId
{
    static constexpr uint32_t None = 0;

    // metadata items known to DebugVisualizer
    static constexpr uint32_t Timestamp = 1;
    static constexpr uint32_t ProcessName = 2;
    static constexpr uint32_t ProcessId = 3;
    static constexpr uint32_t ThreadId = 4;

    // custom metadata (plugin-specific) start at this offset
    static constexpr uint32_t CustomBase = 100;
};



/*
    That's the interface exposed by the plugin DLL
    and called by DebugVisualizer.

    Threading model:
    DebugVisualizer guarantees that all calls to functions of an instance 
    of this interface are serialized. Interface functions never execute 
    concurrently for a given instance.
*/
class PluginInterface
{
public:
    static constexpr uint32_t version = 1;

    // Return details about the plugin itself.
    virtual
    DVP_Status
    GetPluginDescription(
        PluginDescription& desc
        ) = 0;

    // Return an array of MetadataDescription objects.
    // Each item describes a custom metadata item implemented by the plugin.
    // Returns DVP_Status::NotSupported if no custom metadata is implemented.
    virtual
    DVP_Status
    GetMetadataDescription(
        uint32_t& numElements,
        const MetadataDescription*& descArray
        ) = 0;

    // Show plugin configuration settings (dialog window).
    // Returns DVP_Status::NotSupported if no such dialog is implemented.
    virtual
    DVP_Status
    ShowSettings(
        void* parentWindowHandle
        ) = 0;

    // Read configuration data as needed.
    virtual
    DVP_Status
    Configure() = 0;

    // Start capturing trace data.
    virtual
    DVP_Status
    Start() = 0;

    // Stop capturing trace data.
    virtual
    void
    Stop() = 0;

};

/*
    A future version of the plugin interface will add new functions
    but still needs to support all existing functions.
    It would be defined as follows:

    class PluginInterfaceV2 : public PluginInterface
    {
    public:
        static constexpr uint32_t version = 2;

        virtual
        DVP_Status        
        NewFunction() = 0;

    };
*/



/*
    That's the interface exposed by DebugVisualizer and 
    called by code in the the plugin DLL.

    Threading model:
    The plugin is allowed to call interface functions concurrently
    from multiple threads.
*/
class HostInterface
{
public:
    static constexpr uint32_t version = 1;

    virtual
    DVP_Status
    DeliverEvent(
        const char* text,
        const MetadataItem* metadata  // nullptr or chain of metadata items
        ) = 0;
        
    virtual
    DVP_Status
    DeliverEvent(
        const wchar_t* text,
        const MetadataItem* metadata  // nullptr or chain of metadata items
        ) = 0;

    
    //
    // Write a parameter to the persistent configuration database managed by DebugVisualizer.
    // 
    // Addressing of parameters is hierarchical using three levels:
    // 1st level: uniqueId reported in PluginDescription
    // 2nd level: section name passed to this function
    // 3rd level: parameter name passed to this function
    //
    virtual
    DVP_Status
    WriteConfig(
        const wchar_t* section,
        const wchar_t* parameter,
        const wchar_t* value
        ) = 0;

    //
    // Read a parameter from the persistent configuration database managed by DebugVisualizer.
    // Returns DVP_Status::NotPresent if the given paramater is not present in the database.
    // Returns DVP_Status::BufferTooSmall if the provided buffer is too small to receive the data,
    // sizeNeeded (if not nullptr) will be set to the required size in this case.
    //
    virtual
    DVP_Status
    ReadConfig(
        const wchar_t* section,
        const wchar_t* parameter,
        wchar_t* buffer,
        uint32_t bufferSize,  // in wchar_t
        uint32_t* sizeNeeded  // in wchar_t, optional, can be set to nullptr
        ) = 0;

};


} //namespace dvplugin
