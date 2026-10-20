# SoftTarget LE

Golpes auto-direcionados para Skyrim LE (1.9.32.0 + SKSE 1.7.3), sem Papyrus e sem travar a mira.
Ao atacar, o personagem vira para o inimigo mais proximo dentro de um cone a frente.

Estado: versao 0.1, ainda nao testada no jogo.

## Como compilar (tudo no GitHub)
1. Envie estes ficheiros para o repositorio (mantendo as pastas).
2. Aba **Actions** > "Build SoftTarget LE" > **Run workflow** (ou ele corre sozinho a cada envio).
3. No fim, baixe o artefacto **resultado**: contem `SoftTargetLE.dll` e `build.log`.

## Como instalar para testar
- `SoftTargetLE.dll` em `Data/SKSE/Plugins/`
- `SoftTargetLE.ini` (da pasta `dist/`) em `Data/SKSE/Plugins/`
- Log do jogo: `Documentos/My Games/Skyrim/SKSE/SoftTargetLE.log`

## Licenca
GPL-3.0. A configuracao de compilacao e os enderecos da LE vem do FreeClimb LE
(baseado no FreeClimb de Eelta, GPL-3.0).
