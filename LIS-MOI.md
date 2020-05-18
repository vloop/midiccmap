## Résumé

Applique un pitch bend selon la note active.

Ceci permet un microtuning sur les synthés mono qui ne l'ont pas d'origine.
Ceci ne peut marcher qu'en mode mono: le pitch bend est un message de canal, si on joue un accord le même pitch bend s'applique à toutes les notes.

## Installation

L'installation est manuelle.

Les seuls prérequis sont alsa et fltk

Les commandes suivantes ont été testées sous Linux Mint 19.1

- Installation des fichiers de développement ALSA:
```
sudo apt install libasound2-dev
```
- Installation de FLTK et de ses fichiers de développement:
```
sudo apt install libfltk1.3-dev
```
- Compiation:
```
gcc autobend.c -o autobend -lfltk -lasound -lpthread -lstdc++
```
- Installation:
```
sudo cp autobend /usr/local/bin/
sudo cp po/fr/autobend.mo /usr/share/locale/fr/LC_MESSAGES/
```
## Utilisation
```
autobend [file]
```
L'utilisation des curseurs avec la souris permet seulement un ajustement sommaire,
pour ajuster finement il faut utiliser les raccourcis clavier flèche haut et bas, + et - sur le pavé numérique,
ou la molette de défilement de la souris.
Les touches Suppr, Début et Fin fixent la valeur à 0, -8192 et +8191 respectivement.

Autobend ne peut pas connaître la plage du pitch bend sur votre synthétiseur.
La plage des messages pitch bend midi est fixe de -8192 à 8191
mais l'intervalle musical correspondant dépend de votre synthé, l'accord doit être fait à l'oreille.

L'accord peut être enregistré dans un fichier de type .conf.
Il s'agit d'un simple fichier texte, avec une syntaxe élémentaire:
chaque ligne est de la forme "note espace décalage", par exemple E -2048.

## Remerciements
Merci à jmechmech pour l'idée de départ et les tests.
