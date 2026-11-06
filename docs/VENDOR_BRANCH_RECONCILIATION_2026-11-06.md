# Vendor — revue des trois branches historiques (2026-11-06)

## Mise à jour — réintégrations effectives des lots R07/R09/R10

Les décisions de non-fusion en bloc ci-dessous étaient **temporaires**, prises lors de l'audit de branches historiques. Elles ont été remplacées par un portage contrôlé et un import épinglé.

- **R09 neural FinalHead → ViT : repris et fusionné** dans la [PR vendor #8](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/8), commit de fusion [`380cb334`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/380cb334a4763cb6f33d6d2c515d96a2712c43fe). Sources GPU historiques restaurées, contrat de validation stricte des formes/buffers ajouté. Branche moteur liée : [PR #27](https://github.com/niakw/ProsperoEden-Encore/pull/27) (CMake, géométrie ViT, tests C++ Clang/GCC UBSan et SPIR-V Vulkan 1.1). Le test hôte [Action #42310811334](https://github.com/niakw/ProsperoEden-Encore/actions/runs/42310811334) a réussi. Ne pas confondre le raccord de graphes natifs avec l'activation finale du modèle DLSS-NR.
- **Overrides R07/R10 : repris et fusionnés** dans [encore-overrides PR #1](https://github.com/niakw/encore-overrides/pull/1) au commit [`95f21edf`](https://github.com/niakw/encore-overrides/commit/95f21edf13bfbd2888602f2b419dd856d6d0fbfa). Audit et tests de génération NS2 verts. Snapshot complet (et non l'ancien commit partiel `b359111`) intégré au vendor dans [PR #9](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/9), commit [`6b7bc406`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/6b7bc4068e3cdaa0ff708a197b46dcf433445ae7), avec provenance `VENDOR-ORIGIN.json`. Import SHA-pinné et testé par [Action #42310669721](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42310669721) ; workflow privilégié éphémère supprimé après exécution.
- **R10 chargeur HD RomFS** (`af5f16d`) : non fusionné; audit et portage distincts nécessaires (ne pas le confondre avec l'anisotropie R10).
- Les anciens commits des trois branches restent accessibles dans l'ascendance de `main`, même si leurs refs ont été nettoyées. La sélection du vendor dans le moteur doit suivre le dernier commit fusionné; les CI P0–P4 et la validation console restent des contrôles séparés.

---



Ce document conserve la provenance **avant** retrait éventuel des références de branches. Il ne constitue **pas** une attestation de compilation, d'intégration fonctionnelle, ni de validation PS5. Référence canonique du suivi : [roadmap moteur #15](https://github.com/niakw/ProsperoEden-Encore/issues/15).

| Branche | Commit à conserver | Écart à l'audit du 06/11 | Décision technique |
| --- | --- | --- | --- |
| `feat/overrides-ns2-fsr1-defaults` | [`b3591111268046cbff1267e62a3552570b009da6`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/b3591111268046cbff1267e62a3552570b009da6) | 1 commit propre, 832 de retard | **Ne pas fusionner en bloc.** Modifie 8 992 profils NS1 de Bilinear à FSR 1 sans preuve PS5, et embarque un workflow hebdomadaire avec `contents: write` qui appelle `tools/sync_switch2_games.py`, absent du snapshot `sources/encore-overrides/tools` dans `main`. Le fichier `VENDOR-ORIGIN.json` y pointe vers un autre état d'`niakw/encore-overrides`. Reprendre séparément la politique de qualité, les profils NS2 et l'anisotropie après validation de la source canonique et de la génération, **sans changement massif par défaut**. |
| `feat/r09-rdna2-neural-noise` | [`c609409ba5c1a6c8a6c566d99361833413513b02`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/c609409ba5c1a6c8a6c566d99361833413513b02) | 1 commit propre, 731 de retard | **Implémentation incomplète, ne pas fusionner directement.** Ajoute `NrRdna2VitBoundaryRepack`, une passe compute 4×4 tiles ↔ raster et son branchement dans `NrRdna2VitRun`, mais inclut `rdna2_nr_vit_boundary_repack_policy.h` **absent également de cette branche** ; l'enregistrement SPIR-V/CMake et les tests de descripteurs/barrières Vulkan restent à vérifier. Reprendre sur le socle actuel avec tests hôte, sans activer une fonction fictive. |
| `feat/r10-textures-separate-romfs-loader` | [`af5f16da3ab32a72ac03b959637e9bf310130393`](https://github.com/niakw/Prospero.Eden-Encore-vendor/commit/af5f16da3ab32a72ac03b959637e9bf310130393) | 1 commit propre, 832 de retard | **Candidat à porter après audit.** Ajoute ~91 lignes à `patch_manager.cpp` pour charger `textures/<TITLE_ID>/<PACK>/romfs` à partir d'un manifeste JSON et superposer ces fichiers uniquement dans RomFS, en laissant les mods traditionnels prioritaires. Vérifier la durée de vie de `RealVfsFilesystem`, les liens symboliques/chemins descendants, la validation du manifeste, le budget mémoire et le résultat sur RomFS; ne pas prétendre que cela fonctionne déjà sur PS5. |

## Politique de clôture

- Ne pas merger les anciens commits directement : ils ont été construits sur des bases aujourd'hui fortement divergentes, et deux comportent des dépendances/automatismes manquants.
- Ne pas perdre leur **historique Git**, leur code ou leurs liens : conserver les trois SHA dans un commit de provenance et, si nécessaire, dans l'ascendance de `main`, avant toute suppression de ref.
- Ne jamais utiliser ces branches comme source d'activation par défaut, ni changer en masse les overrides sans preuve.
- L'issue maître #15 reste le suivi unique des jalons du moteur. Le contenu ci-dessus est **une revue/archivage de branches**, pas trois nouveaux points P0–P4.
- Seule la validation de code porté **dans le nouveau socle** et des CI du couple moteur/vendor peut autoriser un statut « intégré ». Les preuves PS5 restent en P5.
