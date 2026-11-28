# encore-active/ — overrides finaux Eden Encore

encore-active/ est l'unique arbre de code maintenu par Eden Encore dans le vendor.

## Contrat de propriété

1. Toute modification locale d'une dépendance de `sources/` vit ici.
2. Toute nouvelle source, header, CMake, ressource runtime, stub, shim, table de complétude, adaptation NS1/NS2 ou adaptation PS5 propre à Eden Encore vit ici.
3. Le nom de dépendance doit rester le même que dans `sources/`.
4. Lorsqu'un fichier upstream existe, son override conserve exactement le même chemin relatif.
5. Lorsqu'un fichier est nouveau et n'existe pas upstream, il est créé au chemin logique qu'il aurait dans cette dépendance. L'absence d'un fichier de base dans `sources/` n'interdit jamais sa présence dans `encore-active/`.
6. Lorsqu'un fichier existe ici et dans `sources/`, le build sélectionne la version `encore-active/`, jamais une copie modifiée de `sources/`.
7. Un seul owner actif est autorisé pour un chemin logique donné. Les anciennes racines fonctionnelles ne doivent pas rester simultanément sélectionnables après migration.
8. Un stub, shim, fallback, placeholder ABI, table IPC, source de complétude ou fichier actuellement non utilisé ne doit pas être supprimé uniquement parce qu'il n'a pas d'équivalent upstream ou de consommateur immédiat. Sa suppression exige une preuve séparée qu'il est réellement obsolète et sans rôle de compatibilité, de complétude ou de sélection future.
9. Une amélioration d'un fichier canonique plus récent est prioritaire sur une ancienne copie historique. Une migration ne doit jamais restaurer aveuglément une ancienne version par-dessus un owner canonique plus récent.

Exemple :

- `sources/eden/src/core/foo.cpp`
- `encore-active/eden/src/core/foo.cpp`

Le second est l'owner final compilé.

Pour un fichier Encore nouveau :

- pas de `sources/eden/src/core/ns2_feature_stub.cpp` nécessaire ;
- le fichier peut exister directement comme `encore-active/eden/src/core/ns2_feature_stub.cpp`.

## Règle de migration et de suppression

Une ancienne racine ou un ancien fichier n'est supprimé qu'après les contrôles suivants :

1. identifier le fichier ancien réellement versionné ;
2. identifier son propriétaire logique final sous `encore-active/<dépendance>/` ;
3. s'il existe déjà une destination, comparer les deux contenus et déterminer lequel est réellement actif/récent ; ne jamais écraser automatiquement le canonique ;
4. si l'ancien fichier est le propriétaire actif, copier exactement son contenu au chemin canonique ;
5. si le fichier est nouveau, créer le chemin canonique même si aucun fichier homologue n'existe dans `sources/` ;
6. vérifier le contenu de l'ancien et du nouveau fichier après migration — byte-for-byte lorsque la migration doit être pure ;
7. migrer CMake, includes, tests, workflows, générateurs et outils vers le chemin canonique ;
8. vérifier qu'aucune référence active ne pointe encore vers l'ancien chemin ;
9. seulement alors supprimer l'ancien fichier/racine ;
10. après suppression, vérifier de nouveau le graphe de sélection et la CI. Une suppression de chemin n'est jamais une preuve que son comportement a été conservé.

Les migrations avec collision doivent être fail-closed : en cas de doute entre une ancienne copie et un owner récent, conserver les deux le temps d'établir lequel doit devenir le canonique ; ne rien supprimer.

## Ce qui est interdit

- modifier `sources/` pour un correctif Eden Encore ;
- ajouter un nouveau fichier Eden Encore sous `sources/` ;
- supprimer un stub/shim/fichier de complétude faute d'équivalent upstream ;
- supposer qu'un renommage Git signifie que tout le contenu fonctionnel a été conservé ;
- écraser un owner canonique récent par une ancienne copie sans comparaison ;
- garder deux owners actifs pour le même fichier ;
- créer de nouvelles racines fonctionnelles comme `eden-gpu`, `eden-vulkan`, `eden-core`, `ns2-runtime`, etc. ;
- générer au build une copie C++ modifiée d'un fichier upstream lorsque l'override peut être versionné ici ;
- faire dépendre l'admission d'un fichier d'une allowlist de commits/tree/blob SHA.

Les SHA restent légitimes pour le gitlink exact, la provenance, l'intégrité d'artefacts ou des preuves historiques. Ils ne doivent pas décider quel owner source courant est valide.

## Migration historique clôturée — 2026-11-27

Les anciennes racines fonctionnelles `ns2-runtime`, `eden-vulkan` et `amd-fsr31` ont été repliées dans `encore-active/eden` après comparaison des collisions, des consommateurs et des owners réellement sélectionnés.

- les propriétaires NS2 sont aux mêmes chemins logiques sous `encore-active/eden/src/` ;
- le renderer Vulkan maintenu vit sous `encore-active/eden/src/video_core/renderer_vulkan/` ;
- l'intégration AMD FSR 3.1 vit sous `encore-active/eden/externals/amd-fsr31/`.

Ces anciennes racines sont désormais interdites par la gate de layout et ne doivent jamais être recréées. La cible finale est atteinte : même dépendance, même chemin logique, un seul owner actif, aucun contenu supprimé faute d'équivalent upstream.

`sources/` est la dépendance figée. `encore-active/` est notre version finale.
