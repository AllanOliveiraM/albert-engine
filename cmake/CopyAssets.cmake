# Keep the working parts out of the executable directory.
file(REMOVE_RECURSE "${DESTINATION}")
file(MAKE_DIRECTORY "${DESTINATION}")
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}"
     PATTERN "imagery" EXCLUDE)
