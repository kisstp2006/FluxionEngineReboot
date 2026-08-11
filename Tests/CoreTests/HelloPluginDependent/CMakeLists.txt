add_test_plugin(HelloPluginDependent)

file(GENERATE OUTPUT "$<TARGET_FILE_DIR:HelloPluginDependent>/HelloPluginDependent.plugin" CONTENT
"{\"name\":\"HelloPluginDependent\",\"library\":\"HelloPluginDependent\",\"version\":1,\"type\":\"Runtime\",\"dependencies\":[\"HelloPlugin\"]}\n"
)
