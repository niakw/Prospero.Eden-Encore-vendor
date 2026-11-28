# sources/ — dépendances vendor figées

sources/ contient uniquement les dépendances tierces/upstream importées dans le vendor Eden Encore.

## Contrat

- `sources/<dépendance>/` est le miroir vendor de la dépendance choisie.
- Ce contenu est figé pendant le développement courant.
- Une modification de `sources/` n'est autorisée que lors d'une mise à jour manuelle et explicite de la dépendance elle-même.
- Aucun correctif Eden Encore, backport local, instrumentation, optimisation, stub, shim, fichier de complétude, nouveau runtime, header ou CMake propre au projet ne doit être ajouté sous `sources/`.
- Un fichier upstream à modifier est repris au même chemin relatif sous `../encore-active/<dépendance>/`, puis modifié là.
- Un nouveau fichier Eden Encore qui n'existe pas upstream est créé uniquement sous `../encore-active/<dépendance>/`, à l'emplacement logique qu'il aurait dans cette dépendance.
- Il n'est pas nécessaire qu'un chemin ou un fichier existe d'abord dans `sources/` pour pouvoir créer son équivalent Encore.
- `sources/` ne sert pas de zone de staging, de scratch, de génération ou d'override.
- Les stubs, shims, tables de complétude et fichiers Encore sans équivalent upstream appartiennent à `encore-active/`, pas ici.

Exemple d'override :

- base figée : `sources/eden/src/video_core/foo.cpp`
- version Eden Encore : `encore-active/eden/src/video_core/foo.cpp`

Exemple de nouveau fichier propre à Encore :

- aucun fichier requis dans `sources/eden/src/video_core/` ;
- création directe de `encore-active/eden/src/video_core/ns2_feature_stub.cpp`.

Le build peut lire `sources/` comme base, mais lorsqu'un owner correspondant existe dans `encore-active/`, l'owner actif doit être celui de `encore-active/`.

## Mise à jour manuelle d'une dépendance

Une vraie mise à jour de `sources/` est séparée du travail d'override :

1. mettre à jour explicitement la dépendance vendor ;
2. conserver sa provenance et ses licences ;
3. ne pas réinjecter automatiquement les overrides Encore dans `sources/` ;
4. revalider ensuite les fichiers correspondants de `encore-active/` contre la nouvelle base ;
5. ne supprimer aucun override, stub ou shim uniquement parce que l'upstream a changé : vérifier d'abord son rôle et son contenu.

## Protection pendant les migrations

Lors du nettoyage d'anciennes racines `encore-active/`, ne jamais utiliser `sources/` comme destination de secours. Tout fichier ancien encore nécessaire est déplacé vers son chemin canonique sous `encore-active/<dépendance>/`. Avant de supprimer l'ancien fichier, sa destination et son contenu doivent avoir été vérifiés conformément à `../encore-active/README.md`.

En résumé : `sources/` = vendor upstream pur et figé ; `encore-active/` = toutes les modifications et tous les ajouts Eden Encore.

Voir aussi : `../encore-active/README.md`.
