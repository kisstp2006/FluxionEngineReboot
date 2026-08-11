add_test_plugin(CyclicPluginA)

file(GENERATE OUTPUT "$<TARGET_FILE_DIR:CyclicPluginA>/CyclicPluginA.plugin" CONTENT
"{\"name\":\"CyclicPluginA\",\"library\":\"CyclicPluginA\",\"version\":1,\"type\":\"Runtime\",\"dependencies\":[\"CyclicPluginB\"]}\n"
)
