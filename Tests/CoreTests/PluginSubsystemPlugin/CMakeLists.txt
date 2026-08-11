add_test_plugin(PluginSubsystemPlugin)

# Unlike the other fixtures, this one calls FLUXION_SUBSYSTEM_ID_OF, which
# calls the real (non-inline) Fluxion_HashBytes64 -- needs Foundation
# actually linked in, not just its headers visible.
target_link_libraries(PluginSubsystemPlugin PRIVATE Fluxion::Foundation)

file(GENERATE OUTPUT "$<TARGET_FILE_DIR:PluginSubsystemPlugin>/PluginSubsystemPlugin.plugin" CONTENT
"{\"name\":\"PluginSubsystemPlugin\",\"library\":\"PluginSubsystemPlugin\",\"version\":1,\"type\":\"Runtime\",\"dependencies\":[]}\n"
)
