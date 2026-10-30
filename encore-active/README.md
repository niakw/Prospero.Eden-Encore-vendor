# Encore Active : vrais overrides de sources

`vendor/sources/eden` = original conservé ; `vendor/encore-active` = versions maintenues.

| Chemin | Usage |
| --- | --- |
| `eden-threading/src/core/hle/kernel/k_thread.cpp` | Source compilée remplaçant l'original ; finalisation hors verrou |
| `eden-threading/src/core/hle/kernel/slab_helpers.h`, `k_worker_task_manager.h` | En-têtes maintenus copiés tels quels avant compilation, sans patch CMake |
| `eden-threading/src/core/hle/kernel/k_scheduler.cpp`, `k_thread.h`, `k_slab_heap.h` | Overrides coordonnés du threading |
| `eden-threading/src/core/hle/kernel/k_address_arbiter.cpp`, `k_condition_variable.cpp` | Sources HLE natives |
| `eden-threading/src/core/hle/kernel/svc/*.cpp` | Sources SVC natives |
| `eden-sync/src/core/hle/kernel/k_synchronization_object.h` | En-tête natif de synchronisation, aliasing corrigé |
| `eden-threading/native-hle-overrides.cmake` | Sélection de sources CMake, **pas** un correctif C++ généré |
| `eden-core/src/core/hle/service/nvdrv/core/container.cpp` | Session nvdrv : acquisition/libération du processus avec verrou relâché avant fermeture |
| `eden-core/src/hid_core/frontend/emulated_controller.cpp` | Vibrations virtuelles : source maintenue en remplacement du générateur CMake |
| `eden-core/src/core/memory.cpp` | A32 fastmem source issue du backport de production ; garde explicite 32 bits, activation conditionnelle |
| `compiler-rt/emutls.c` | Source du runtime TLS natif maintenue, `EMUTLS_SKIP_DESTRUCTOR_ROUNDS=1`, sans génération CMake |
| `eden-build/src/CMakeLists.txt` | Configuration de build maintenue copiée avant compilation |
| `eden-core/src/common/thread.cpp`, `src/common/fs/fs.cpp` | Sources runtime natives maintenues ; noms de workers et énumération des répertoires |
| `eden-core/src/common/cpu_features.cpp`, `src/common/fs/path_util.cpp` | Détection de fréquence TSC et racines utilisateur/cache natives, auparavant générées par CMake |
| `eden-core/src/video_core/renderer_opengl/gl_texture_cache.cpp` | Diagnostic de copie GL en profil DEV, source C++ maintenue |
| `eden-core/src/video_core/renderer_opengl/{gl_buffer_cache,gl_query_cache}.cpp` | Correctifs GL de buffers et requêtes maintenus, conditions de compilation conservées |
| `eden-core/src/video_core/video_core_opengl.cpp` | Fabrique OpenGL-only maintenue ; fabrique Vulkan épinglée sélectionnée intacte |
| `eden-core/include/core/file_sys/bis_factory.h`, `eden-core/src/core/file_sys/bis_factory.cpp` | ABI BIS et choix du firmware/dossiers mods maintenus hors génération CMake |
| `eden-core/src/core/file_sys/fsmitm_romfsbuild.cpp` | Contextes RomFS sans cycles de propriété ; deux types de remplacement désormais en C++ maintenu |
| `eden-core/src/audio_core/sink/sink_stream.cpp` | File PCM, upmix et trame de maintien via source audio maintenue ; anciens générateurs supprimés |
| `eden-core/src/core/hle/service/filesystem/filesystem.cpp` | Racine firmware maintenue en C++, sans unité dérivée par CMake |
| `eden-core/src/core/internal_network/network_interface.cpp` | Choix de la branche FreeBSD pour l'énumération des interfaces, sans génération CMake |
| `eden-core/src/core/hle/kernel/svc/svc_ipc.cpp`, `src/core/hle/service/service.cpp` | Mesures IPC/HLE du profil diagnostic, sélectionnées sans génération CMake |
| `eden-core/src/core/hle/service/{filesystem,nvdrv,nvnflinger}/` | Sources diagnostiques de fichiers, événements NVDRV et files de présentation |
| `eden-gpu`, `eden-vulkan`, `eden-core`, `ps5-mesa` et autres dossiers | Autres adaptations maintenues ; voir `docs/EDEN_OVERRIDE_MIGRATION_INVENTORY.md` |

| `eden-core/src/video_core/buffer_cache/buffer_cache.h` | Header GPU R437 complet et maintenu ; sélection PS5 par copie sans réécriture CMake |

| `eden-build/tooling/native/app_crt_radv.cpp` | Startup RADV sélectionné directement, avec initialiseur IEEE et preuve de parité source |

| `eden-core/src/dynarmic/` | Traducteur A64, lecture/écriture scalaire et optimiseur IR : trois sources complètes compilées sans génération CMake |

| `eden-core/src/video_core/engines/maxwell_3d.cpp` | Diagnostic DEV de condition de rendu, versionné avec parité source et sélection exclusive |

| `eden-core/src/common/sparse_large_vector.h` | Header mémoire sparse complet, sélection par copie et oracle d’équivalence des onze substitutions |

| `eden-core/src/dynarmic/backend/x64/{reg_alloc.h,reg_alloc.cpp}` | Deux unités maintenues du gestionnaire de registres ; sélection explicite et parité des cinq anciennes injections |

| `eden-core/src/dynarmic/backend/x64/constant_pool.cpp` | Alias mémoire du pool JIT, source versionnée avec sélection exclusive |

| `eden-core/src/dynarmic/backend/x64/{emit_x64_vector.cpp,emit_x64_data_processing.cpp}` | Deux émetteurs scalaires complets, trois anciennes substitutions de CMake supprimées ; injection mémoire toujours X08 |

| `eden-core/src/video_core/engines/sw_blitter/blitter.cpp` | Blitter logiciel natif : backport de production préservé et trois tailles élargies, sans reconstruction CMake |

| `eden-core/src/shader_recompiler/frontend/maxwell/translate/impl/not_implemented.cpp` | PRMT native source avec vecteurs de référence et diagnostic borné, sélectionnée directement |

| `eden-core/src/video_core/engines/fermi_2d_{observed,experimental}.cpp` | Deux variantes GPU complètes ; profil expérimental OFF par défaut, sélecteur CMake sans génération |

| `eden-build/third_party/xbyak/xbyak.h` | En-tête Xbyak 7.40.1 complet avec huit adaptations d'alias, copié sans transformations CMake (X09 reste indirect) |

| `eden-core/src/core/hle/kernel/k_process.cpp` | Finalisation du processus DEV : cinq étapes chronométrées, sélection directe, comparaison du résultat historique |

| `eden-core/src/dynarmic/backend/block_range_information.{h,cpp}` | Gestion des blocs par pages : deux sources complètes sélectionnées sans générateur CMake |

| `eden-core/src/video_core/renderer_opengl/gl_rasterizer{,_dev}.cpp` | Deux unités normal/DEV de diagnostic de draw, sélectionnées directement sans réécriture CMake |

| `eden-core/src/video_core/dma_pusher{,_dev}.cpp` et `engines/puller{,_dev}.cpp` | Commandes GPU invité : variantes normales/DEV maintenues et parité G20 |

Chaîne HLE : `headless/CMakeLists.txt` -> `eden-threading/native-hle-overrides.cmake` -> fichiers C++/en-têtes maintenus.

Tests associés : `tools/verify/hle/`. Ces tests ne sont pas inclus dans l'application native.

Pour le runtime et les diagnostics, `headless/CMakeLists.txt` ne fait
que sélectionner les unités maintenues et retirer la source d'origine de
la cible concernée. Il ne doit plus générer de copies C++ pour ces unités.
Les sources amont épinglées restent inchangées dans `vendor/sources/eden`.

Les 81 sources et en-têtes de `eden-backports`, et deux fichiers CMake
supplémentaires, forment 83 fichiers de backports livrés en
production depuis le snapshot épinglé, contrôlés par empreintes et installés
par `tools/install-vendored-eden-backports.py` (pas de `git apply` au build).

Catalogue exhaustif des fichiers C/C++ actifs : `docs/EDEN_ACTIVE_SOURCE_CATALOG.md`
(286 fichiers enregistrés et rattachés au graphe technique ; ce lien n’atteste pas leur exécution sur console).
Le recensement des fichiers n'est pas une validation de leurs fonctions.
Le générateur `renderer_opengl.cpp` n'est plus nécessaire : le renderer
original est sélectionné directement, sans créer une copie maintenue identique.

L'inventaire couvre **5 appels générateurs** du CMake principal et **12**
dans les modules, avec **2 modules indirects** de substitution suivis
séparément ; ce ne sont pas des décomptes de fichiers uniques. Chaque nouvelle migration doit
conserver un test de parité et de sélection native.

**Dette non encore consolidée ici :** `headless/backports/` contient
46 patches sur le snapshot Eden épinglé : 44 appels `apply_one` depuis
`tools/apply-eden-backports.sh` et deux patches installés depuis
`tools/install-active-core-backports.py`. Leurs chemins et leur statut
sont catalogués dans `docs/EDEN_OVERRIDE_MIGRATION_INVENTORY.md`.
Ils ne sont **pas** des sources C++ maintenues, et il ne faut pas les
compter comme 46 fichiers de compilation supplémentaires.
