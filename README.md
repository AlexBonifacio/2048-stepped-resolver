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

Lancer un essai sans publier automatiquement les stats globales:

```bash
make run-test TEST_ARGS="--solver hybrid --session test"
```

Lancer les tests:

```bash
make test
```

Nettoyer les binaires/objets:

```bash
make clean
```

## Options du programme

`--session NOM`  
Obligatoire. Sauvegarde/recharge le plateau dans `data/sessions/NOM.json`.

`--solver hybrid|expectimax`  
Choisit le solveur. `hybrid` combine expectimax + simulations Monte Carlo.

`--explain`  
Affiche le classement chiffre des coups avant la suggestion. Utile pour comprendre pourquoi l'IA prefere un coup.

`--quality fast|balanced|strong`  
Regle la puissance de recherche. `strong` est plus lent mais analyse plus de scenarios.

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
Lance `N` parties automatiques avec les stats globales existantes, sans modifier les JSON.

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

Donc plus tu publies de vraies parties, plus le modele peut s'adapter aux spawns de debut/milieu/fin de partie.

## Modele IA

Le solver utilise expectimax + rollouts Monte Carlo. L'evaluation du plateau combine:

- maintien du plus gros bloc dans un coin stable
- organisation en serpent autour de ce coin
- cases libres
- potentiel de fusion
- monotonie ligne/colonne inspiree de `nneonneo/2048-ai`

Difference importante avec le 2048 classique: les spawns ne sont pas forces en 90/10. Ils utilisent `data/observed_spawns.json`, donc les simulations et suggestions restent adaptees aux tuiles observees dans ce jeu.

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

## Profils de puissance

Note: plus gros n'est pas toujours meilleur. Le solver reste heuristique; des parametres enormes peuvent amplifier une mauvaise intuition de l'evaluateur. Pour jouer serieusement, commence par High ou Very high avec `--explain`.

Low, rapide pour tester:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 3 --rollouts 80 --candidates 2 --chance-cells 8 --max-rollout-moves 50"
```

Medium, equivalent au mode par defaut:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 4 --rollouts 200 --candidates 2 --chance-cells 12 --max-rollout-moves 80"
```

High, profil recommande actuellement pour vraie partie:

```bash
make run-game GAME_ARGS="--solver hybrid --session mimmo --depth 5 --rollouts 500 --candidates 3 --chance-cells 16 --max-rollout-moves 120"
```

Very high, plus lent mais plus robuste:

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

`--quality strong` correspond environ a un profil entre High et Very high:

```text
depth 6, rollouts 800, candidates 4, chance-cells 16, max-rollout-moves 140
```
