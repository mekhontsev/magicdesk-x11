execute_process(COMMAND "${MAKEKEYS}" keysymdef.h XF86keysym.h Sunkeysym.h DECkeysym.h HPkeysym.h
        OUTPUT_FILE "${OUTPUT}" COMMAND_ERROR_IS_FATAL ANY)
