# 2048 ranks helper

Assistant CLI pour reproduire une partie de 2048 "ranks", demander une suggestion IA, sauvegarder/reprendre des sessions, et apprendre les apparitions observees.

## Commandes Make

Compiler:

```bash
make
```

Afficher les exemples de lancement:

```bash
make run
```

Lancer une vraie partie avec sauvegarde obligatoire:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo"
```

Lancer en mode plus lent mais plus fort:

```bash
make run-game GAME_ARGS="--quality strong --solver hybrid --session session_name"
```

Lancer en mode tres lourd pour analyser une position avancee:

```bash
make run-game GAME_ARGS="--quality godlike --solver hybrid --session session_name"
```

Lancer un essai sans publier automatiquement les stats globales:

```bash
make run-test TEST_ARGS="--solver hybrid --session test"
```

Lancer les tests:

```bash
make test
```

Creer un dossier/zip Windows apres avoir compile `2048-ranks.exe`:

```bash
make package-windows WINDOWS_PACKAGE_ARGS="--solver-exe 2048-ranks.exe"
```

Nettoyer les binaires/objets:

```bash
make clean
```

## Version Windows simple

Le dossier Windows final est fait pour des utilisateurs non techniques:

```text
Lancer 2048 Helper.bat
```

L'utilisateur dezippe le dossier, double-clique ce fichier, garde la fenetre ouverte, et le navigateur s'ouvre automatiquement.

Le zip final doit contenir:

- `Lancer 2048 Helper.bat`
- `2048-ranks.exe`
- `web/`
- `data/`
- optionnel: `python/` pour eviter d'installer Python sur le PC de l'utilisateur

Le launcher cherche d'abord `python/python.exe` dans le dossier. S'il n'existe pas, il utilise Python installe sur Windows.

## Options du programme

`--session NOM`  
Obligatoire. Sauvegarde/recharge le plateau dans `data/sessions/NOM.json`.

`--solver expectimax|hybrid|optimistic|human|target`
Choisit le solveur. `hybrid` est le mode recommande pour jouer serieusement: il combine expectimax, probabilites de spawn et simulations Monte Carlo. `expectimax` reste utile pour comparer rapidement. `optimistic`, `human` et `target` sont experimentaux pour analyser des positions avancees.

`--explain`  
Affiche le classement chiffre des coups avant la suggestion. Utile pour comprendre pourquoi l'IA prefere un coup.

`--quality fast|balanced|strong|godlike`
Regle la puissance de recherche. Par defaut: `strong`, profondeur 6. `godlike` pousse la profondeur, les rollouts et toutes les cases de spawn pour les positions avancees.

`--depth N`  
Profondeur expectimax.

`--rollouts N`  
Nombre de simulations Monte Carlo pour le solver `hybrid`.

`--candidates N`  
Nombre de meilleurs coups testes par les rollouts.

`--chance-cells N`  
Nombre de cases vides considerees dans les scenarios de spawn. `16` = toutes les cases possibles.

`--max-rollout-moves N`  
Longueur maximale des simulations Monte Carlo.

`--start-score N`  
Force le score de depart, utile quand tu ressaisis une partie deja avancee.

`--start-moves N`  
Force le nombre de coups de depart. Important pour ne pas biaiser les stats contextuelles.

`--stats read-only|read-write`  
`run-game` utilise `read-write`, mais avec une session les vraies stats ne sont publiees que via `p`.

`--simulate N`  
Lance `N` parties automatiques depuis un plateau neuf avec deux cases `1`, sans modifier les JSON. Avec `--session NOM`, les apparitions observees dans `data/sessions/NOM.json` enrichissent le modele de generation des tuiles.

`--sim-max-moves N`
Limite chaque simulation a `N` coups. Pratique pour comparer vite des profils quand les donnees apprises prolongent beaucoup les parties.

`--sim-stats global|session|merged`
Choisit les donnees utilisees pour generer les tuiles en simulation. Par defaut: `session`. `session` lit seulement la session donnee par `--session`, `global` lit seulement `data/observed_spawns.json`, et `merged` combine les deux.

`--start-from-session`
Avec `--simulate N --session NOM`, repart exceptionnellement du plateau sauvegarde au lieu de demarrer de zero.

`--sim-report NOM`  
Ajoute un resume des simulations dans `data/simulation_reports/NOM.jsonl`. Le rapport garde les scores, coups, max et parametres, mais pas les coups joues.

`--seed N`  
Fixe la graine des simulations pour comparer deux profils sur une base plus stable.

## Commandes en jeu

`z` ou `w`  
Haut.

`s`  
Bas.

`q` ou `a`  
Gauche.

`d`  
Droite.

`p`  
Publie les apparitions temporaires de la session vers `data/observed_spawns.json`.

`x`  
Quitte et sauvegarde la session.

`r`  
Repart sur un nouveau plateau en gardant les stats temporaires de la session.

`u`  
Annule le dernier changement.

`u 3`  
Annule plusieurs changements.

`e`  
Mode edition du plateau. Valeur `0` vide une case.

`c`  
Corrige le contexte: score actuel et nombre de coups actuel.

`h`  
Affiche l'aide.

## Coordonnees

Les colonnes sont `a b c d`, les lignes `1 2 3 4`.

Ces formats sont acceptes:

```text
a1
1a
1 1
```

`a1` et `1a` designent la meme case.

## Sessions et stats

Chaque session garde:

- `score`
- `moves`
- `cells`
- `map`, une version lisible du plateau en 4x4
- les apparitions temporaires non publiees
- `solved`, les coups joues par le joueur avec le plateau avant/apres
- `outcome`, le resultat final connu de la partie

Exemple:

```json
"map": [
  [2, 0, 2, 2],
  [0, 0, 3, 6],
  [0, 0, 1, 8],
  [1, 0, 0, 9]
]
```

Les stats globales sont dans:

```text
data/observed_spawns.json
```

Le modele apprend les apparitions avec contexte:

```text
valeur apparue + nombre de coups + plus gros bloc
```

Le champ `solved` enregistre les decisions. `source` vaut `human` pour les coups joues/rejoues par toi ou depuis une video; les branches automatiques peuvent utiliser `ai`:

```json
{"direction": "left", "source": "human", "moves": 120, "score": 1800, "highest": 8, "gained": 32, "before": [0, 1, 2, 3], "after": [1, 2, 3, 0]}
```

Les tableaux `before` et `after` contiennent toujours 16 ranks dans les vrais fichiers.

Le champ `outcome` sert a apprendre des echecs et des reussites:

```json
{
  "status": "ended",
  "target": 13,
  "final_score": 53780,
  "final_moves": 2037,
  "final_highest": 12,
  "success": false,
  "reason": "no_moves"
}
```

Le site remplit automatiquement `outcome` si la partie n'a plus de coup possible. Tu peux aussi utiliser `Marquer fin` pour clore une partie manuellement.

## Site web

Lancer le site:

```bash
python3 web/server.py
```

Pour une vraie partie commencee avant d'ouvrir l'appli:

1. cree ou ouvre une session;
2. reproduis le plateau actuel en ranks;
3. saisis le score reel dans `score actuel`;
4. clique `Estimer coups`;
5. ajuste le nombre de coups si tu as une meilleure estimation;
6. clique `Appliquer contexte`.

Le score et les coups sont sauvegardes dans le JSON de session. C'est important parce que le modele de spawn utilise le nombre de coups et la plus grosse tuile pour choisir les probabilites.

Donc plus tu publies de vraies parties, plus le modele peut s'adapter aux spawns de debut/milieu/fin de partie.

## Modele IA

Le solver recommande utilise expectimax + rollouts Monte Carlo. Expectimax pondere les branches par la probabilite d'arriver a chaque etat apres le coup initial, avec une table de transposition pour reutiliser les positions deja vues.

L'evaluation du plateau combine:

- maintien du plus gros bloc dans un coin stable
- organisation en serpent autour de ce coin avec la matrice `2,4,8,16 / 256,128,64,32 / 512,1024,2048,4096 / 65536,32768,16384,8192` et ses orientations
- cases libres
- penalite forte si une branche arrive sur un plateau sans coup possible
- potentiel de fusion
- monotonie ligne/colonne
- homogeneite des valeurs voisines
- heuristique de lignes inspiree de `nneonneo/2048-ai`

Difference importante avec le 2048 classique: les spawns ne sont pas forces en 90/10. Par defaut, les simulations et suggestions utilisent uniquement les sessions JSON, par exemple `data/sessions/youtube_max01_VozVoz.json`. `data/observed_spawns.json` n'est utilise que si tu demandes explicitement `global` ou `merged`.

## Recettes utiles

Reprendre une partie:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo"
```

Reprendre avec le mode fort:

```bash
make run-game GAME_ARGS="--quality strong --solver hybrid --session mimmo"
```

Reprendre avec le classement des coups:

```bash
make run-game GAME_ARGS="--quality strong --solver hybrid --session mimmo --explain"
```

Ressaisir un plateau deja avance:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --start-score 2564 --start-moves 181"
```

Publier une session terminee:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo"
```

Puis taper:

```text
p
```

Lancer quelques simulations rapides:

```bash
./2048-ranks --simulate 10 --quality fast --solver hybrid --seed 42
```

Lancer des simulations et conserver un rapport separe:

```bash
./2048-ranks --simulate 10 --quality fast --solver hybrid --seed 42 --sim-report fast_baseline
```

Comparer un profil plus fort:

```bash
./2048-ranks --simulate 10 --solver hybrid --depth 5 --rollouts 500 --candidates 3 --chance-cells 16 --max-rollout-moves 120 --seed 42 --sim-report high_profile
```

Reprendre une partie resolue par un humain au milieu de sa trace, puis laisser l'IA simuler la suite avec des apparitions aleatoires:

```bash
./2048-ranks --session youtube_max01_VozVoz --simulate 20 --solver hybrid --quality strong --sim-stats session --start-from-solved 0.5 --checkpoint-source human --seed 42 --sim-report youtube_midpoint_ai
```

Tu peux aussi cibler un nombre de coups precis:

```bash
./2048-ranks --session youtube_max01_VozVoz --simulate 20 --solver hybrid --quality strong --checkpoint-move 800 --checkpoint-source human
```

## Profils de puissance

Note: plus gros n'est pas toujours meilleur. Le solver reste heuristique; des parametres enormes peuvent amplifier une mauvaise intuition de l'evaluateur. Pour jouer serieusement, commence par High ou Very high avec `--explain`.

Low, rapide pour tester:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 3 --rollouts 80 --candidates 2 --chance-cells 8 --max-rollout-moves 50"
```

Medium, equivalent au mode `balanced`:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 4 --rollouts 200 --candidates 2 --chance-cells 12 --max-rollout-moves 80"
```

High, profil recommande actuellement pour vraie partie:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 5 --rollouts 500 --candidates 3 --chance-cells 16 --max-rollout-moves 120"
```

Very high, equivalent au mode par defaut `strong`:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 6 --rollouts 1000 --candidates 4 --chance-cells 16 --max-rollout-moves 160"
```

Godlike, experimental. Tres lent, pas forcement meilleur que High:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 7 --rollouts 2500 --candidates 4 --chance-cells 16 --max-rollout-moves 220"
```

Raccourcis integres:

```bash
make run-game GAME_ARGS="--quality fast --solver hybrid --session mimmo"
make run-game GAME_ARGS="--quality balanced --solver hybrid --session mimmo"
make run-game GAME_ARGS="--quality strong --solver hybrid --session mimmo"
```

`--quality strong` correspond a:

```text
depth 6, rollouts 1000, candidates 4, chance-cells 16, max-rollout-moves 160
```
