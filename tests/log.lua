di.log.log_level = "warn"
di.log("warn", "asdf")
di.log.log_target = di.log.file_target("./log.txt", true)
di.log("error", "asdf")
