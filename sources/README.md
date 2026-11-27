# sources/ — vendor pur

Ce répertoire contient uniquement les sources tierces/upstream importées dans le vendor Eden Encore.

## Règle de source de vérité

- `sources/` est un **miroir vendor pur**.
- Les arbres sous `sources/<projet>/` doivent rester conformes à leur dépôt/source upstream d'origine.
- Aucun correctif Eden Encore ne doit être écrit directement dans un arbre `sources/`.
- Aucun nouveau fichier C/C++/header/CMake propre à Eden Encore ne doit être ajouté sous un arbre `sources/`, même si ce fichier n'existe pas dans le dépôt upstream.
- Aucun backport adapté, instrumentation, compatibilité PS5, NS1/NS2, workaround ABI ou optimisation locale ne doit être matérialisé dans `sources/`.
- Si Eden Encore doit modifier un fichier upstream, la version maintenue va dans `../encore-active/`.
- Si Eden Encore crée un fichier qui n'existe pas upstream, ce fichier va également dans `../encore-active/`.
- Le build ne doit jamais dépendre d'une modification locale cachée dans `sources/`.

Le rôle de `sources/` est uniquement de fournir la base upstream/vendored intacte et les dépendances tierces nécessaires.

Le seul contenu spécifique Eden Encore autorisé directement à la racine de ce répertoire est cette documentation de rôle. Les sous-arbres vendored restent purs.

Voir aussi : `../encore-active/README.md`.
