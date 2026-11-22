# TPE: AF mínimo a partir de uma gramática regular (código em C)

## Compilar

```
gcc -std=c99 -Wall -Wextra -o afd_minimo afd_minimo.c
```

No Windows (MinGW/Code::Blocks) é o mesmo comando, e o executável sai como `afd_minimo.exe`.

## Executar

| Comando | O que faz |
|---|---|
| `./afd_minimo` | Usa a gramática da máquina de café e depois pede palavras para testar (Enter vazio sai) |
| `./afd_minimo cuup ccp` | Testa as palavras passadas e encerra |
| `./afd_minimo -g gramatica_cafe.txt` | Lê a gramática de um arquivo |

O programa imprime, em ordem: 1) a gramática G = (V, T, P, S); 2) o AFN obtido da GR;
3) o AFD obtido por subconjuntos; 4) a tabela de marcação de pares da minimização;
5) o AFD mínimo (Q, Σ, δ, q0, F); 6) a simulação passo a passo de cada palavra.
Ele também gera o arquivo `afd_minimo.jff`, que abre no JFLAP (File → Open) e serve para fazer a figura do artigo.

## Problema de exemplo: máquina de café (café a R$ 2,00)

| Símbolo | Significado |
|---|---|
| `c` | moeda de R$ 0,50 |
| `u` | moeda de R$ 1,00 |
| `p` | botão "pedir café" |
| `x` | botão "cancelar" (devolve o crédito) |

As variáveis representam o crédito acumulado: S = R$ 0,00, A = 0,50, B = 1,00, C = 1,50, D = 2,00 e E = 2,50.
Uma palavra é **aceita** quando termina com o café liberado.

Exemplos: `cuup` e `ccccp` são aceitas; `ccp` (crédito insuficiente) e `cuupc` (ação depois de entregar o café) são rejeitadas.

**Resultado da minimização:** o AFD tem 8 estados e o mínimo tem 7, porque **D (R$ 2,00) e E (R$ 2,50) são equivalentes**:
nos dois, uma moeda a mais é devolvida, `p` libera o café e `x` volta a S.
Para a linguagem, que só diz se o café foi liberado, essas duas situações são iguais.
O troco é uma saída da máquina e não muda a resposta do AFD.
Uma máquina de Mealy ou de Moore manteria D e E separados.

## Trocar de problema

Escreva um `.txt` no mesmo formato de `gramatica_cafe.txt` e rode `./afd_minimo -g seu_arquivo.txt`. O código não precisa ser alterado.

- Variáveis: uma letra maiúscula. Terminais: uma letra minúscula ou um dígito.
- Produções no formato `X -> aY | a | &` (gramática linear à direita; `&` é a palavra vazia).
- Linhas `simbolo : texto` dão nome aos símbolos; `qF : texto` descreve o estado final que o programa cria.
- A primeira produção define o símbolo inicial.
