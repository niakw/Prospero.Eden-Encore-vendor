# Nintendo Switch 2 — profils Encore et synchronisation

**5 novembre 2026 — profils candidats, non validés par build PS5.**

Le catalogue de jeux NS2 coexiste avec celui de Switch 1 ; aucun profil
ancien n'est remplacé.

## Sources et identifiants

Source communautaire : https://www.switchbrew.org/wiki/Switch_2:_Title_list/Games
Le générateur télécharge uniquement des métadonnées HTML, aucune ROM.

Les jeux NS2 natifs utilisent des Title IDs 0400… à 16 caractères
hexadécimaux, dont les applications de base se terminent par 000.
Exemples : Mario Kart World 0400C3F00006E000, Donkey Kong Bananza
040074A01BF12000.

Les « Nintendo Switch 2 Edition » d'anciens jeux ont souvent des IDs
0100… communs avec leurs versions NS1. Elles sont enregistrées dans
metadata/ns2_editions.json avec auto_apply=false et ne peuvent donc
pas écraser une fiche Switch 1. Les IDs dupliqués sous deux noms
incompatibles sont ignorés, pas arbitrairement attribués.

## Défaillances évitées et profils retenus

Profils généraux NS2 :
- Minimum : jeu 0,75×, sortie 1080p, Bilinear, AA Off.
- Recommandé : jeu 1×, sortie 1440p, FSR 1 spatial à 40 %, FXAA.
- Haute : jeu 1,25×, sortie 1440p, FSR 1 à 50 %, SMAA Ultra.
- Ultra : jeu 1,5×, sortie 2160p, FSR 1 à 50 %, SMAA Ultra.

Il s'agit d'une politique **provisoire non mesurée sur PS5**. Ni la
résolution native ni le FPS des jeux NS2 ne sont inventés. Les
overrides manuels restent prioritaires, et le renderer NS2 doit
être qualifié séparément. Le FSR 3.1 temporel n'est pas activé
par ces profils.

Les profils sont dans general/ns2/ ; les jeux déjà connus sont
dans games/ avec source_console Nintendo Switch 2, et dans manifest.json.

## Génération et workflow

Prévisualisation : python3 tools/sync_switch2_games.py --limit 10
Génération réelle : python3 tools/sync_switch2_games.py --write

Chaque jeu est ouvert en création exclusive ; un fichier existant
n'est jamais écrasé. Si Switchbrew est indisponible, si la structure
est incohérente ou si la validation échoue, aucun nouveau commit
ne doit être publié.

Workflow : .github/workflows/weekly-switch2-game-sync.yml,
le lundi à 06:03 UTC, séparément du workflow NS1. Il exécute
les tests des cas d'erreur, synchronise les nouveaux jeux,
vérifie toutes les combinaisons renderer/filtre et publie uniquement
les ajouts. Les métadonnées NS2 Editions sont maintenues à part.

Tests :
- python3 -m unittest discover -s tools -p test_sync_switch2_games.py -v
- python3 tools/audit_graphics_recommendations.py

## Intégration native PS5

Le build PS5 utilise une copie versionnée dans vendor. Une
nouvelle source de profils doit être épinglée et le snapshot C++
régénéré avec le bon SHA avant d'être activé sur la console.
La présence de profils dans ce dépôt seul ne prouve pas
qu'ils sont déjà déployés sur PS5.
