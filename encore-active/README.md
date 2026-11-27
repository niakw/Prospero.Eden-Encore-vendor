# encore-active/ — overrides finaux Eden Encore

encore-active/ est l'unique arbre de code maintenu par Eden Encore dans le vendor.

## Contrat

1. Toute modification locale d'une dépendance de sources/ vit ici.
2. Toute nouvelle source, header, CMake, ressource runtime ou adaptation propre à Eden Encore vit ici.
3. Le nom de dépendance est le même que dans sources/.
4. Lorsqu'un fichier upstream existe, son override conserve exactement le même chemin relatif.
5. Lorsqu'un fichier est nouveau, il est placé à l'emplacement logique qu'il aurait dans cette dépendance, sans créer une racine nommée d'après un ticket, une fonctionnalité, NS1/NS2 ou une plateforme.
6. Lorsqu'un fichier existe ici et dans sources/, le build doit sélectionner la version encore-active/, jamais patcher la copie de sources/ pendant le build.

Exemple :

- sources/eden/src/core/foo.cpp
- encore-active/eden/src/core/foo.cpp

Le second est l'owner final compilé.

## Ce qui est interdit

- modifier sources/ pour un correctif Eden Encore ;
- ajouter un nouveau fichier Eden Encore sous sources/ ;
- garder deux owners actifs pour le même fichier ;
- créer de nouvelles racines fonctionnelles comme eden-gpu, eden-vulkan, eden-core, ns2-runtime, etc. ;
- générer au build une copie C++ modifiée d'un fichier upstream alors que l'override peut être versionné ici ;
- faire dépendre la sélection d'un fichier d'une liste de commits SHA.

## Migration en cours

Les racines historiques encore présentes (eden-core, eden-vulkan, ns2-runtime, ns2-astc, amd-fsr31) sont de la dette de structure. Elles doivent être repliées vers la dépendance correspondante sous encore-active/<dépendance>/, puis supprimées. Aucun nouvel owner ne doit y être ajouté.

La cible finale est simple : même dépendance, même chemin logique, un seul owner actif.

sources/ est la dépendance figée. encore-active/ est notre version finale.
