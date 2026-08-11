add_test_plugin(HelloPlugin)

file(GENERATE OUTPUT "$<TARGET_FILE_DIR:HelloPlugin>/HelloPlugin.plugin" CONTENT
"{\"name\":\"HelloPlugin\",\"library\":\"HelloPlugin\",\"version\":1,\"type\":\"Runtime\",\"dependencies\":[]}\n"
)
