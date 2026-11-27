# sources/ — dépendances vendor figées

Ce répertoire contient les dépendances tierces/upstream importées dans le vendor Eden Encore.

## Contrat

- `sources/` est un **répertoire de dépendances figées**.
- Les sous-arbres `sources/<projet>/` reflètent la source upstream/vendor importée et ne sont pas des propriétaires Eden Encore.
- **Aucun build, script, CI, correctif runtime, backport, générateur ou outil automatique ne doit modifier un fichier sous `sources/`.**
- Une modification de `sources/` n'est autorisée que lors d'une **mise à jour vendor manuelle, explicite et volontaire** d'une dépendance upstream.
- Après cette mise à jour manuelle, l'arbre importé doit de nouveau correspondre à la dépendance upstream choisie ; on n'y applique pas de patch Encore.
- Aucun nouveau fichier C/C++/header/CMake propre à Eden Encore ne doit être ajouté dans un sous-arbre `sources/`, même si ce fichier n'existe pas upstream.
- Toute modification locale d'un fichier upstream va dans `../encore-active/`.
- Tout nouveau fichier propre à Eden Encore qui n'existe pas upstream va également dans `../encore-active/`.
- Le build doit consommer les fichiers de `sources/` comme dépendances de base immuables et sélectionner un owner de `encore-active/` lorsqu'un override maintenu existe.

En résumé : **`sources/` = dépendances upstream figées ; `encore-active/` = code maintenu par Eden Encore.**

Le seul contenu spécifique Eden Encore autorisé directement à la racine de ce répertoire est cette documentation de rôle ; les sous-arbres vendored restent purs. Une correction de sécurité, un backport ou une mise à jour de lockfile spécifique à Encore ne constitue pas une exception : elle va dans `../encore-active/<dépendance>/`.

## Symétrie des chemins d'override

Pour une dépendance `sources/<dépendance>/`, l'override correspondant utilise **le même nom de dépendance et le même chemin relatif** sous `encore-active/<dépendance>/`.

Exemple :

- base : `sources/eden/src/video_core/foo.cpp`
- override : `encore-active/eden/src/video_core/foo.cpp`

Si un fichier Encore est nouveau et n'existe pas upstream, il est créé sous `encore-active/<dépendance>/` à l'emplacement logique qu'il occuperait dans cette dépendance. Les noms de lots, tickets, plateformes ou fonctionnalités ne doivent pas créer une seconde arborescence de runtime.


Voir aussi : `../encore-active/README.md`.
