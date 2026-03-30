set(srcs
    "simple_video_server_example.c")

set(html_files
    "${PROJECT_DIR}/frontend/gzipped/index.html.gz"
    "${PROJECT_DIR}/frontend/gzipped/loading.jpg.gz"
    "${PROJECT_DIR}/frontend/gzipped/favicon.ico.gz"
    "${PROJECT_DIR}/frontend/gzipped/assets/index.js.gz"
    "${PROJECT_DIR}/frontend/gzipped/assets/index.css.gz")

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS "."
                       EMBED_TXTFILES ${html_files})
