# Run a replay twice through stmalo-sim: every command must succeed and
# both runs must print exactly the same output.  Skipped without game data.
#
# cmake -DSIM=<stmalo-sim> -DREPLAY=<file.jsonl> -P replay.cmake

if("$ENV{STMALO_DATA}" STREQUAL "")
	message("SKIP: STMALO_DATA not set")
	return()
endif()
foreach(run 1 2)
	execute_process(COMMAND ${SIM} INPUT_FILE ${REPLAY}
	    OUTPUT_VARIABLE out${run} RESULT_VARIABLE rc)
	if(NOT rc EQUAL 0)
		message(FATAL_ERROR "stmalo-sim exited with ${rc}")
	endif()
endforeach()
if(out1 MATCHES "\"ok\":false")
	message(FATAL_ERROR "a command failed:\n${out1}")
endif()
if(NOT out1 STREQUAL out2)
	message(FATAL_ERROR "the two runs differ")
endif()
string(LENGTH "${out1}" len)
message("replay ok, ${len} bytes of output")
