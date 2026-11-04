# tsMuxeR Dolby Vision sur NAS Synology (Docker)

L'application est la même que sous Windows : tsMuxeR 2.20.0 (fork teaching-droid :
Dolby Vision double couche, fusion TrueHD + noyau AC3, sortie MKV…) avec nos ajouts
(file d'attente, ajout de dossier, ISO Blu-ray en entrée, fusion TrueHD + AC3 automatique, nom de
sortie d'après l'entrée, journal détaillé), et MediaInfo.
Elle s'affiche dans un navigateur à l'adresse `http://<ip-du-nas>:5800`.

## Prérequis

- Un NAS à processeur **Intel ou AMD** (modèles « + » : DS920+, DS923+, DS224+…).
  Les modèles ARM (série « j ») ne conviennent pas : l'image est construite pour x86-64.
- **DSM 7.2** avec le paquet **Container Manager** (Centre de paquets).
- Le fichier image **`tsmuxer-dv.tar`** (dossier `dist` du projet, ~250 Mo).

Si une version précédente de l'image est installée, ses réglages ne sont pas repris
(ils étaient rangés sous un autre nom) : l'interface redémarre en français avec les
réglages par défaut.

## 1. Importer l'image

1. Copiez `tsmuxer-dv.tar` sur le NAS avec File Station (par exemple dans `docker`).
2. **Container Manager > Image > Action > Importer > Ajouter depuis le fichier sur le NAS**,
   choisissez `tsmuxer-dv.tar`. L'image `tsmuxer-dv:latest` apparaît dans la liste.

## 2. Préparer les dossiers

- Créez le dossier des réglages : `docker/tsmuxer` (File Station).
- Repérez le partage qui contient vos films, par exemple `video` → `/volume1/video`.

## 3. Identifiant de votre utilisateur DSM

Les fichiers créés appartiendront à cet utilisateur. En SSH : `id votre_utilisateur`
(le premier compte administrateur est souvent `uid=1026`, groupe `users` = `100`).

## 4. Créer le projet

**Container Manager > Projet > Créer**

- Nom : `tsmuxer`
- Chemin : `/docker/tsmuxer`
- Source : **Créer docker-compose.yml**, collez le contenu de `docker-compose.yml`
  (dossier `docker` du projet) et ajustez :
  - `/volume1/video:/storage` → votre partage vidéo,
  - `USER_ID` / `GROUP_ID` → les valeurs de l'étape 3,
  - pour protéger l'accès par mot de passe, décommentez `SECURE_CONNECTION` et les lignes
    `WEB_AUTHENTICATION...` : l'adresse devient alors `https://<ip-du-nas>:5800` et le
    navigateur signale un certificat auto-signé la première fois (à accepter).
- Terminez l'assistant : le conteneur démarre.

## 5. Utilisation

Ouvrez `http://<ip-du-nas>:5800` dans un navigateur.

- L'interface démarre en français, avec `/storage` (votre partage) comme dossier de sortie.
- Mode sombre (tsMuxeR, MediaInfo et le panneau du navigateur) : `DARK_MODE: "1"` dans
  le docker-compose, `"0"` pour revenir au thème clair (puis redémarrer le projet).
- **Écrivez toujours dans `/storage`** (ou un de ses sous-dossiers) : tout autre
  emplacement est interne au conteneur et serait perdu à sa mise à jour.
- **Ajouter dossier** : choisissez d'abord le type de sortie (ISO, dossier Blu-ray, M2TS…),
  le dossier de sortie et les options, puis un dossier à analyser. Chaque MKV ou ISO Blu-ray trouvé
  (sous-dossiers compris) devient une tâche de la file d'attente, avec toutes ses pistes, nommée
  d'après le fichier source (pour un ISO, la playlist la plus longue). Sont ignorés et listés : les
  films déjà convertis (sortie existante), les ISO créés par tsMuxeR (conversions précédentes), les
  ISO chiffrés et les fichiers illisibles. Lancez ensuite la file.
- **Ajouter fichier** accepte aussi une **image ISO Blu-ray**, lue directement sans la monter.
  La playlist la plus longue (le film) est prise. Si plusieurs playlists ont une durée proche,
  une liste permet de choisir. L'ISO doit être une copie déchiffrée : une image chiffrée (AACS)
  est refusée avec un message.
- **TrueHD (Atmos)** : quand un MKV contient une piste TrueHD et une piste AC3 de la même langue
  (de préférence celle nommée « compatibility »), les deux sont fusionnées automatiquement, comme
  sur le disque d'origine. Les lecteurs Blu-ray qui ne décodent pas le TrueHD lisent alors l'AC3.
  La fusion se modifie ou se retire dans les options de la piste TrueHD.
- **Explorer un disque…** (ou clic droit sur un fichier issu d'un Blu-ray, ou ajout d'un
  `index.bdmv`) : toutes les playlists d'un ISO ou d'un dossier Blu-ray, avec durée, taille,
  chapitres, pistes et clips. La plus longue est sélectionnée ; les playlists courtes et les copies
  sont masquées. « Charger cette playlist » la met en entrée ; « Ajouter la sélection à la file »
  crée une tâche par playlist (épisodes d'une série), nommée « disque - playlist ».
- **Sélection automatique des pistes** (onglet Général) : à l'ajout d'un fichier, seules les pistes
  audio et sous-titres des langues choisies restent cochées (ex. audio `fra, eng`, sous-titres
  `fra`), avec l'option d'écarter commentaires, audiodescription ou un mot du nom.
  Pour le français, `vff`, `vfq` ou `vfi` ne gardent que ce doublage (ex. `vff, eng`), reconnu
  d'après l'étiquette `fr-FR`/`fr-CA` ou le nom de la piste ; la colonne langue l'affiche
  (`fra (VFQ)`). Une piste dont la langue est `und` prend celle de son nom (« French (Canada) »).
  S'applique aussi à « Ajouter dossier ».
- **Taille estimée** (à droite des types de sortie) : calculée d'après les pistes cochées, avec le
  disque conseillé (BD-25, 50, 66 ou 100). Disponible pour les MKV (mkvmerge, MakeMKV…).
- **La file d'attente est conservée** dans `docker/tsmuxer` : après un redémarrage du conteneur
  ou du NAS, les tâches en attente réapparaissent (une tâche interrompue repart du début). Il
  suffit de relancer la file.
- Clic droit sur un fichier d'entrée → **Ouvrir dans MediaInfo**. MediaInfo s'ouvre en
  plein écran ; fermez-le par **File > Exit** pour revenir à tsMuxeR (la langue de
  MediaInfo se change dans *Options > Preferences*).
- Le volet qui s'ouvre avec la languette à gauche de l'écran donne accès au
  presse-papiers (copier/coller un chemin) et au plein écran.
- Le multiplexage continue si vous fermez l'onglet du navigateur ; rouvrez la page
  pour suivre la file d'attente.

## Mise à jour

Importez le nouveau `tsmuxer-dv.tar` (étape 1), puis **Projet > tsmuxer > Action >
Construire** (ou Arrêter puis Démarrer). Les réglages de `docker/tsmuxer` sont conservés.

## Reconstruire l'image (sur le PC)

```
docker build -f docker/Dockerfile -t tsmuxer-dv:latest .
docker save tsmuxer-dv:latest -o dist/tsmuxer-dv.tar
```
