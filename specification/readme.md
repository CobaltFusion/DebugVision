
@startuml

class PluginDescription << (S,#FF7700) struct >>
PluginDescription : wchar_t name[Constants::MaxStringSize]
PluginDescription : wchar_t provider[Constants::MaxStringSize]
PluginDescription : wchar_t uniqueId[Constants::UniqueIdSize]
PluginDescription : uint32_t majorVersion
PluginDescription : uint32_t minorVersion

class MetadataDescription << (S,#FF7700) struct >>
MetadataDescription : uint32_t id
MetadataDescription : const wchar_t* name

class MetadataItem << (S,#FF7700) struct >>
MetadataItem : uint32_t id
MetadataItem : const wchar_t* data
MetadataItem : const MetadataItem* next

MetadataItem -> MetadataItem : next

class PluginInterface << (I,#FF7700) interface >>
PluginInterface : virtual DVP_Status GetPluginDescription(PluginDescription& desc)

class HostInterface << (I,#FF7700) interface >>
HostInterface : virtual DVP_Status DeliverEvent(const char* text, const MetadataItem* metadata)

@enduml
