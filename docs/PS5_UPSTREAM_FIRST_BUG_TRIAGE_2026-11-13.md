# Eden Encore vendor — upstream-first PS5 source triage

**13 novembre 2026.** Référence canonique, actualisée côté moteur : [protocole de diagnostic upstream-first](https://github.com/niakw/ProsperoEden-Encore/blob/main/docs/UPSTREAM_FIRST_PS5_DIAGNOSTIC_PROTOCOL_2026-11-13.md), associé à [roadmap #15](https://github.com/niakw/ProsperoEden-Encore/issues/15).

## Règles pour toutes les corrections vendor

- Ne **jamais** traiter un crash, des saccades, des textures manquantes, une vidéo/HUD, un chargement ou un warning sans rechercher et comparer les issues, PR, commits, releases et forks upstream pertinents (Eden, Eglantine, Lichii, nxnative, PS5/Mesa/RADV, etc.).
- Commencer par prouver quel **fichier vendor est sélectionné par le CMake natif PS5**. Un correctif sur une copie Eden non compilée ne change pas la PS5. Vérifier moteur HEAD, gitlink vendor, profils, build flags, ABI et dépendances avant portage. Préserver les chemins NS1 et NS2 qui ne sont pas concernés.
- Pour tout port : origine exacte upstream, problème et preuves, source ancienne/nouvelle, impact rendu/threads/GPU/kernel/fences, tests host/sanitizers négatifs et positifs, CI sur SHA ; **ne pas parler de qualification console sans test sur binaire et jeux réels**.
- Pour les temps de frame, distinguer code invité, GPU invité, pipeline create/wait, GPU fence, buffer/texture contention, compilations et image Vulkan de chargement du launcher. Comparer cache froid et chaud, début de match et deuxième lancement. Ne pas promettre un gain de FPS PC sur RDNA2 PS5.
- Pour les textures, inclure formats, strides, sources/sous-rectangles MSAA, descripteurs, sampler, invalidation et barrière/lifetime. Pour le HUD, distinguer fonts du launcher et atlas/guest shader. Pour la vidéo, conserver paquets refusés et ordre de sortie selon l'owner NS1/NS2.
- Ne pas masquer compiler warnings ni CodeQL pour atteindre le vert ; ne pas contourner les vérifications de provenance/snapshot gelé PSBC. Associer le gitlink moteur aux vraies corrections et recontrôler le scanner après intégration.

## État des branches de travail vendor (instantané 13/11)

**main** : 1ec7b429238d1175a76dc94b7c0a7f1c2eb62864. PR ouvertes (non validées console) : [#105](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/105) update RomFS extrait, [#106](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/106) JXR/WebP, [#107](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/107) NS1 FFmpeg EAGAIN, [#108](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/108) NS1 Vulkan MSAA offset, [#109](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/109) NS2 AM CMIF. **Ne pas supprimer ces cinq branches non intégrées**.

La branche **feat/ps5-jxr-selected-only-decode** est identique à main (0 commit en avance et en retard) et est **candidate à supprimer comme référence redondante** ; la branche **feature/nov13-ps5-jxr-selected-only** porte encore sept commits distincts correspondant à #106. Ne pas confondre les deux.

La source [Lichii #1413](https://github.com/zortos293/Lichii/pull/1413) expose un deadlock sparse GPU pendant MKW sur PC, **pas encore démontré dans le flux GPU PS5 Encore** ; [Lichii #1592](https://github.com/zortos293/Lichii/pull/1592) corrige un pointeur semaphore dangling d'un propriétaire de puller **non retrouvé tel quel dans le runtime PS5 sélectionné**. Ne pas importer l'un ou l'autre sans identifier les locks/owners équivalents et les tests.

## Nettoyage

Avant fermeture d'une PR : comparer la branche avec main, contrôler toutes ses dépendances et tests, intégrer ou explicitement archiver tout changement distinct, repinner moteur si nécessaire, puis retirer la branche seulement si aucune PR/workflow/gitlink n'en dépend. Faire le même inventaire côté moteur, et garder les issues P0 ouvertes tant que le test PS5 n'est pas concluant.

## Extension du suivi au 13 novembre — cache FC27 et provenance moteur

Référence croisée : [registre moteur / vendor](https://github.com/niakw/ProsperoEden-Encore/blob/main/docs/audits/PS5_2026-11-13_BOTH_REPOS_OPEN_WORK_REGISTER.md).

- **Vrai gitlink compilable moteur** (13/11, contrôle Actions) : `1ec7b429238d1175a76dc94b7c0a7f1c2eb62864`, distinct du `main` vendor **au moment de la comparaison**, alors à `d9f6efc76f95fdfd2893082384cb2115851739cc` (avant les commits documentaires). Toute nouvelle qualification doit utiliser le gitlink du commit moteur testé, pas seulement l'étiquette `main` du vendor.
- **[Vendor PR #110](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/110)** : instrumentation startup-only pour cache shader par titre `vulkan.bin` et cache driver séparé `vulkan_pipelines.bin`, comptage des records rejetés/queue, analyse locale, zéro appel disque supplémentaire au rendu. Sept CI hôtes, dont sécurité + vrai propriétaire R23 gelé, réussies sur [`6d1ac12`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/6d1ac1219ec07fb425a92bc033dc16641471906f). **Pas** de preuve d'amélioration FC27 sur PS5.
- **[Engine PR #128](https://github.com/niakw/ProsperoEden-Encore/pull/128)** : contrôle de provenance compensatoire acceptant exclusivement le blob vendor de #110 après retrait strict de sept blocs instrumentés délimités. Le blob source historique R23 reste inchangé. [CI d'intégration #43517569415](https://github.com/niakw/ProsperoEden-Encore/actions/runs/43517569415) réussie sur le vrai vendor gitlink du moteur **et** le commit de la PR #110 (source générée complète). Aucun pin/merge sur `main` n'a été modifié par ce test.
- **Recherche upstream complétée** : [Lichii #1036](https://github.com/zortos293/Lichii/pull/1036) cache v2/scheduler priorisé, [#1334](https://github.com/zortos293/Lichii/pull/1334)/[#1337](https://github.com/zortos293/Lichii/pull/1337) pipelines gardés en RAM + SPIR-V réutilisé et [#1296](https://github.com/zortos293/Lichii/pull/1296) atlas glyphes no-depth. **Aucun port direct** sans identifier propriétaire et signature PS5 identiques.
- **MKW / Lichii #1413** : les symboles `BackSparseWrite`, `AllocateGpuOnlyBacking`, `GpuSplit` de son deadlock PC ne correspondent pas à un owner sélectionné d'Encore. Notre `encore-active/ns2-runtime/gpu_session/.../gpu_worker.cpp` gère ses réservations et mappings avec IPC HLE et verrous propres ; comparer les ordres de verrous et la progression GPU guest avant tout changement. Aucun diagnostic de deadlock identique confirmé.

**Inventaire actualisé** : 6 PR moteur ouvertes, 6 PR vendor ouvertes, 8 issues moteur ouvertes, aucune issue vendor ouverte. Sept branches non-`main` côté vendor, dont `feat/ps5-jxr-selected-only-decode` sans commit propre (0 avance / 1 retard) : seule candidate au retrait après vérification des dépendances. **Aucune branche supprimée**, aucune PR fusionnée sur la seule base de CI source. L'objectif PS5 (FC27 fluide, Bananza/MKW bootable, launcher complet, warnings et sécurité) reste à qualifier matériellement.
