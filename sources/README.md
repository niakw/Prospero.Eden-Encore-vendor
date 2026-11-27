# sources/ — dépendances vendor figées

sources/ contient uniquement les dépendances tierces/upstream importées dans le vendor Eden Encore.

## Règle

- sources/<dépendance>/ est un miroir vendor de la dépendance choisie.
- Ce contenu est figé pendant le développement courant.
- Une modification de sources/ n'est autorisée que lors d'une mise à jour manuelle et explicite de la dépendance elle-même.
- Aucun correctif Eden Encore, backport local, instrumentation, optimisation, nouveau fichier runtime, header ou CMake propre au projet ne doit être ajouté sous sources/.
- Un fichier upstream à modifier est copié au même chemin relatif sous ../encore-active/<dépendance>/, puis modifié là.
- Un nouveau fichier Eden Encore qui n'existe pas upstream est créé sous ../encore-active/<dépendance>/ à l'emplacement logique qu'il aurait dans cette dépendance.

Exemple :

- base figée : sources/eden/src/video_core/foo.cpp
- version Eden Encore : encore-active/eden/src/video_core/foo.cpp

Le build peut lire sources/ comme base, mais lorsqu'un owner correspondant existe dans encore-active/, l'owner actif doit être celui de encore-active/.

En résumé : sources/ = vendor upstream pur et figé ; encore-active/ = toutes les modifications Eden Encore.

Voir aussi : ../encore-active/README.md.
