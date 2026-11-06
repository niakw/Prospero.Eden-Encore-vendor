# Revue des recommandations graphiques Encore R07/R24

5 novembre 2026. Analyse reproductible des fiches sans promotion automatique.
Commande : python3 tools/audit_graphics_recommendations.py

## Inventaire vérifié

9 460 jeux catalogués, dont **9 459 auto-baseline**
(8 992 Nintendo Switch 1 + 467 Switch 2) sans mesure console
propre au jeu et **une seule fiche spécifique FC 27**
(0100C49025D3E000).

- Minimum : Bilinear, AA Off pour les jeux automatiques (FSR 1 pour le Minimum spécifique FC 27)
- Recommended : **FSR 1 spatial, FXAA** pour les 9 459 jeux automatiques.
  Le profil spécifique FC 27 reste Bilinear/FXAA, sans modification ciblée.
- High : Bicubic/FXAA pour les auto-baselines NS1 ;
  FSR 1/SMAA Ultra sur les nouvelles NS2 ; FC 27 garde sa fiche dédiée.
- Ultra : Bilinear/AA Off sur les anciens profils NS1 (l'image 2× peut
  atteindre la résolution de sortie) ; FSR 1/SMAA Ultra sur NS2.

Le runtime graphique Encore sait lire désormais les clés
lanczos, sgsr, sgsr_edge et fxaa_quality, mais refuse les options
réservées à Vulkan lorsqu'un profil choisit OpenGL. Les anciennes clés, les identifiants persistés et la priorité des overrides
manuels sont conservés. Le choix Recommandé a changé de Bilinear à FSR 1
**uniformément** dans les 8 992 fiches NS1 générées ; cela ne constitue
pas un gain mesuré sur console, et FC 27 n'est pas modifié.
Le script d'audit rejette les combinaisons invalides et évite de
promouvoir ces shaders encore non mesurés dans les milliers de
baselines automatiques.

## Revue ciblée et ordre théorique

Qualité spatiale supposée sur jeux 3D (pas de métrique PS5) :
SGSR Edge > SGSR > FSR 1 > Lanczos > Bicubic > Bilinear > Nearest.
AA des contours : SMAA Ultra > FXAA+ > FXAA > Off.
Le classement peut s'inverser sur certaines scènes et Nearest est
souvent préférable pour pixel art.

Priorité 1 **FC 27** : conserver les réglages existants jusqu'à
comparaison de pelouse, tribunes, joueurs, HUD, scintillement,
VRAM et frametime GPU ; sa baseline inclut FSR 1 en Minimum et
Bilinear dans Recommended/High/Ultra, à dessein pour ménager la GPU.
Priorité 2 **BOTW** : scènes de feuillage, silhouettes, panoramiques,
transparence, texture et mesure GPU. Priorité 3 **NS2** : pas
de revendication tant que les images invitées ne sont pas présentées.

Un changement spécifique à un jeu devra être justifié par des
captures A/B, une séquence de mouvement, la version testée,
les mesures de frametime/VRAM et la qualité du HUD, avec
l'override manuel utilisateur toujours prioritaire.

FSR 3.1 temporel et véritable MSAA x2/x4/x8 ne sont pas implémentés ;
ne jamais les activer par profil ni leur attribuer un bénéfice.
**8 992 auto-baselines NS1 ont été mises à jour uniformément,
467 profils NS2 ont été créés ; aucune modification de la fiche FC 27.
Nouveaux profils non mesurés sur PS5.**
