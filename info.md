# Info recabada

## glib-core/dt.h

Parece que este fichero está dedicado principalmente a la definición de tipos de dato básicos


# Info sobre la compilación

Podemos eliminar snap-exp, tutorials, los ejemplos que no sean node2vec, las funciones de snap-adv que no sean n2v o sus dos dependencias ...
Hay bastantes ficheros que pueden ser eliminados sin ningún tipo de problema

Se utilizan Makefile para compilar.
La base está en snap/

Esta compila los diferentes directorios (el que nos interesa es test, podemos dejar este únicamente)
Dentro de /snap/examples/ tenemos otro makefile al que podemos intruír que únicamente cree el node2vec

Hay otro más sin embargo que se llama Makefile.examin ->
Este makefile es el importante!

Aquí dentro se compila el paquete completo Snap para poderlo integrar con los algoritmos (como node2vec)

Si puedo modificar la compilación de este Snap ya está


Para evitar compilar el test (que requiere de un montón de ficheros que no vienen al caso) podemos modificar el makefile en
/snap-core/Makefile que lo incluye en el all
Para modificar esto, si simplemente eliminamos el all explota porque lo necesita para algo cerca del final de la compilación y revienta

Después de modificar esto podemos finalemente compilar.

Ahora podemos comenzar a borrar todos los ficheros que no deseemos que salen en Snap y todo va.



Ahora estoy teniendo problemas con los tipos de glib-core que importa bd (que no se si tienen que ser todos o se pueden eliminar algunos)


En glib core se podrían eliminar unos cuantos archivos. No obstante simplemente he comentado sus includes en base.h y base.cpp para evitar que se compilen. He modificado ->
- hashmap.h
- xmlser.h
- unicodestring.cpp | unicodestring.h
- wch.cpp | wch.h
- blobbs.cpp | blobbs.h
- url.cpp | url.h
- http.cpp | http.h
- html.cpp | html.h
- ss.cpp | ss.h
- ssmp.cpp | ssmp.h
- json.cpp | json.h
- prolog.cpp | prolog.h
- zipfl.cpp | zipfl.h

No he podido eliminar más dependencias dado que otras de las que están en este fichero, a pesar de no ser necesarias para las funcionalidades de node2vec, tienen dependencias en algunos de los ficheros que sí son necesarios (por lo que he observado snap es algo monolítico en cuanto a sus clases base).
Por ejemplo la clase TXmlObjSer o TXmlDoc que se utiliza en bd.h en bastantes funciones requieren de añadir axml.h y xml.cpp a la compilación.
