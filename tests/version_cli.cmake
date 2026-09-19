foreach(tool IN ITEMS EMOJINEER EMJI LSP)
    if(NOT DEFINED ${tool})
        message(FATAL_ERROR "missing -D${tool}=<path>")
    endif()
endforeach()

execute_process(COMMAND "${EMOJINEER}" --version
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "^emojineer [0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "emojineer --version failed: rc=${rc} out=${out} err=${err}")
endif()

execute_process(COMMAND "${EMJI}" --version
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "^emji [0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "emji --version failed: rc=${rc} out=${out} err=${err}")
endif()

execute_process(COMMAND "${LSP}" --version
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "^emojineer-lsp [0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "emojineer-lsp --version failed: rc=${rc} out=${out} err=${err}")
endif()
