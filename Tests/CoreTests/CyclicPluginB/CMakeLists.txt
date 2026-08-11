add_test_plugin(CyclicPluginB)

file(GENERATE OUTPUT "$<TARGET_FILE_DIR:CyclicPluginB>/CyclicPluginB.plugin" CONTENT
"{\"name\":\"CyclicPluginB\",\"library\":\"CyclicPluginB\",\"version\":1,\"type\":\"Runtime\",\"dependencies\":[\"CyclicPluginA\"]}\n"
)
