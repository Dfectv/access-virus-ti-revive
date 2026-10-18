# factory_classes.cmake - the built plug-in's VST3 classes (moduleinfotool): its own class id and the original Access
# plug-in's, so hosts that ignore moduleinfo compatibility (Ableton Live 11, E221) open projects made with the original.
#   cmake -DTOOL=moduleinfotool -DBUNDLE=<plug-in>.vst3 -DOUT=file.json -DOWN=<cid> -DORIGINAL=<cid> -P factory_classes.cmake
get_filename_component(BUNDLE "${BUNDLE}" REALPATH) # the SDK loader needs the plain bundle path
execute_process(COMMAND ${TOOL} -create -version 0.0.0 -path ${BUNDLE} -output ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "moduleinfotool failed (${rc})")
endif()
file(READ ${OUT} info)
foreach(cid ${OWN} ${ORIGINAL})
    if(NOT info MATCHES "\"CID\": \"${cid}\"")
        message(FATAL_ERROR "class ${cid} missing:\n${info}")
    endif()
endforeach()
message(STATUS "classes ${OWN} and ${ORIGINAL} present")
