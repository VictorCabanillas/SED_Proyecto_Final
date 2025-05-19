idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    # Embed the server root certificate into the final binary
                    EMBED_TXTFILES /home/gonzalo/empotrados/SED_Proyecto_Final/server_certs/ca_cert.pem)
