# Managerul de aplicații și aplicațiile cititorului (FW-6, FW-10). C portabil ca
# nucleul, fără antete ESP-IDF: simulatorul compilează aceleași fișiere (`sim ui`).
idf_component_register(SRCS "ui.c" "perflog.c" "settings.c" "sidecar.c" "app_library.c" "app_reader.c" "app_usb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES core)
