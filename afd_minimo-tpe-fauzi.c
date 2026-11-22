/*
 * TPE - Maquina de cafe (cafe = R$ 2,00)
 * GR -> AFN -> AFD -> AFD minimo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>  /* acentos no Windows */
#endif

/* Limites */
#define MAX_VARS   26
#define MAX_TERMS  36
#define MAX_PRODS  512
#define MAX_AFD    256
#define MAX_LINHA  512
#define MAX_DESC   128
#define MAX_NOME   96

/* Conjunto de estados (em bits) */
typedef unsigned long Conjunto;

/* Producao X -> aY */
typedef struct {
    int lhs;   /* X */
    int term;  /* a */
    int rhs;   /* Y */
} Producao;

/* Gramatica */
typedef struct {
    char vars[MAX_VARS];   int nvars;
    char terms[MAX_TERMS]; int nterms;
    Producao prods[MAX_PRODS]; int nprods;
    char desc[128][MAX_DESC];
    char descFinal[MAX_DESC];
} Gramatica;

/* AFN */
typedef struct {
    int n;
    Conjunto delta[MAX_VARS + 1][MAX_TERMS];
    Conjunto finais;
} AFN;

/* AFD */
typedef struct {
    int n;
    Conjunto conj[MAX_AFD];
    int delta[MAX_AFD][MAX_TERMS];
    int final[MAX_AFD];
    int morto[MAX_AFD];
} AFD;

/* Globais */
static Gramatica G;
static AFN N;
static AFD D;
static AFD M;                     /* minimo */
static int classeDe[MAX_AFD];
static unsigned char marcado[MAX_AFD][MAX_AFD];
static char descMin[MAX_AFD][4 * MAX_DESC];

/* Gramatica da maquina de cafe */
static const char *GRAMATICA_CAFE[] = {
    "# Maquina de cafe da universidade - o cafe custa R$2,00",
    "# Terminais (alfabeto)",
    "c : moeda de R$0,50",
    "u : moeda de R$1,00",
    "p : botao pedir cafe",
    "x : botao cancelar (devolve o credito)",
    "# Variaveis (credito acumulado na maquina)",
    "S : credito R$0,00",
    "A : credito R$0,50",
    "B : credito R$1,00",
    "C : credito R$1,50",
    "D : credito R$2,00",
    "E : credito R$2,50 (troco de R$0,50)",
    "qF : cafe liberado",
    "# Producoes",
    "S -> cA | uB | pS | xS",
    "A -> cB | uC | pA | xS",
    "B -> cC | uD | pB | xS",
    "C -> cD | uE | pC | xS",
    "D -> cD | uD | p | xS",
    "E -> cE | uE | p | xS",
    NULL
};

/* ===== Auxiliares ===== */

/* Erro */
static void erro(const char *msg, int linha)
{
    if (linha > 0)
        fprintf(stderr, "Erro na linha %d da gramatica: %s\n", linha, msg);
    else
        fprintf(stderr, "Erro: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* Tira espacos das pontas */
static char *aparar(char *s)
{
    char *fim;
    while (isspace((unsigned char)*s)) s++;
    fim = s + strlen(s);
    while (fim > s && isspace((unsigned char)fim[-1])) fim--;
    *fim = '\0';
    return s;
}

/* Tira todos os espacos */
static void tirarEspacos(char *s)
{
    char *d = s;
    for (; *s; s++)
        if (!isspace((unsigned char)*s)) *d++ = *s;
    *d = '\0';
}

/* Junta texto */
static void anexar(char *buf, size_t tam, const char *txt)
{
    size_t usado = strlen(buf);
    if (usado + 1 < tam)
        strncat(buf, txt, tam - usado - 1);
}

static int ehVariavel(char c) { return c >= 'A' && c <= 'Z'; }
static int ehTerminal(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/* Indice da variavel */
static int indiceVar(char c)
{
    int i;
    for (i = 0; i < G.nvars; i++)
        if (G.vars[i] == c) return i;
    G.vars[G.nvars] = c;
    return G.nvars++;
}

/* Indice do terminal */
static int indiceTerm(char c, int criar)
{
    int i;
    for (i = 0; i < G.nterms; i++)
        if (G.terms[i] == c) return i;
    if (!criar) return -1;
    G.terms[G.nterms] = c;
    return G.nterms++;
}

/* ===== 1. Ler a gramatica ===== */

/* Le descricao */
static void lerDescricao(char *linha, char *doisPontos, int num)
{
    char *simbolo, *texto;
    *doisPontos = '\0';
    simbolo = aparar(linha);
    texto = aparar(doisPontos + 1);

    if (strcmp(simbolo, "qF") == 0) {
        strncpy(G.descFinal, texto, MAX_DESC - 1);
    } else if (strlen(simbolo) == 1 &&
               (ehVariavel(simbolo[0]) || ehTerminal(simbolo[0]))) {
        strncpy(G.desc[(unsigned char)simbolo[0]], texto, MAX_DESC - 1);
    } else {
        erro("descricao deve comecar com um simbolo (ex.: \"c : moeda\")", num);
    }
}

/* Le producao */
static void lerProducao(char *linha, char *seta, int num)
{
    char *esq, *dir, *alt;
    int lhs;

    *seta = '\0';
    esq = aparar(linha);
    dir = seta + 2;

    if (strlen(esq) != 1 || !ehVariavel(esq[0]))
        erro("o lado esquerdo deve ser UMA variavel maiuscula", num);
    lhs = indiceVar(esq[0]);  /* primeira = inicial */

    for (alt = strtok(dir, "|"); alt != NULL; alt = strtok(NULL, "|")) {
        Producao p;
        tirarEspacos(alt);
        p.lhs = lhs;
        p.term = -1;
        p.rhs = -1;

        if (strcmp(alt, "&") == 0 || strcmp(alt, "\xCE\xB5") == 0) {
            /* X -> & */
        } else if (strlen(alt) == 1 && ehTerminal(alt[0])) {
            p.term = indiceTerm(alt[0], 1);                 /* X -> a  */
        } else if (strlen(alt) == 2 && ehTerminal(alt[0]) && ehVariavel(alt[1])) {
            p.term = indiceTerm(alt[0], 1);                 /* X -> aY */
            p.rhs = indiceVar(alt[1]);
        } else {
            erro("cada alternativa deve ser a, aB ou & (gramatica linear a direita)", num);
        }

        if (G.nprods == MAX_PRODS) erro("producoes demais", num);
        G.prods[G.nprods++] = p;
    }
}

/* Le uma linha */
static void processarLinha(const char *original, int num)
{
    char linha[MAX_LINHA];
    char *s, *seta, *doisPontos;

    strncpy(linha, original, MAX_LINHA - 1);
    linha[MAX_LINHA - 1] = '\0';
    s = aparar(linha);
    if (*s == '\0' || *s == '#') return;

    seta = strstr(s, "->");
    doisPontos = strchr(s, ':');

    if (doisPontos != NULL && (seta == NULL || doisPontos < seta))
        lerDescricao(s, doisPontos, num);
    else if (seta != NULL)
        lerProducao(s, seta, num);
    else
        erro("linha nao e producao (->) nem descricao (:)", num);
}

/* Carrega a gramatica */
static void carregarGramatica(const char *arquivo)
{
    memset(&G, 0, sizeof G);

    if (arquivo == NULL) {
        int i;
        for (i = 0; GRAMATICA_CAFE[i] != NULL; i++)
            processarLinha(GRAMATICA_CAFE[i], i + 1);
    } else {
        char linha[MAX_LINHA];
        int num = 0;
        FILE *f = fopen(arquivo, "r");
        if (f == NULL) erro("nao foi possivel abrir o arquivo da gramatica", 0);
        while (fgets(linha, sizeof linha, f) != NULL)
            processarLinha(linha, ++num);
        fclose(f);
    }

    if (G.nprods == 0) erro("a gramatica nao tem producoes", 0);
}

/* Mostra a gramatica */
static void imprimirGramatica(void)
{
    int v, i, primeira;

    printf("=====================================================\n");
    printf(" 1. GRAMATICA REGULAR  G = (V, T, P, %c)\n", G.vars[0]);
    printf("=====================================================\n");

    printf("V = {");
    for (v = 0; v < G.nvars; v++) printf("%s%c", v ? ", " : "", G.vars[v]);
    printf("}\nT = {");
    for (i = 0; i < G.nterms; i++) printf("%s%c", i ? ", " : "", G.terms[i]);
    printf("}\nSimbolo inicial: %c\n\nP (producoes):\n", G.vars[0]);

    for (v = 0; v < G.nvars; v++) {
        printf("  %c -> ", G.vars[v]);
        primeira = 1;
        for (i = 0; i < G.nprods; i++) {
            const Producao *p = &G.prods[i];
            if (p->lhs != v) continue;
            printf("%s", primeira ? "" : " | ");
            primeira = 0;
            if (p->term < 0) printf("&");
            else printf("%c", G.terms[p->term]);
            if (p->rhs >= 0) printf("%c", G.vars[p->rhs]);
        }
        if (primeira) printf("(sem producoes)");
        if (G.desc[(unsigned char)G.vars[v]][0])
            printf("      [%s]", G.desc[(unsigned char)G.vars[v]]);
        printf("\n");
    }

    printf("\nAlfabeto:\n");
    for (i = 0; i < G.nterms; i++)
        printf("  %c = %s\n", G.terms[i],
               G.desc[(unsigned char)G.terms[i]][0] ? G.desc[(unsigned char)G.terms[i]] : "-");
}

/* ===== 2. GR -> AFN ===== */

/* Nome do estado */
static void nomeEstadoAFN(int e, char *buf, size_t tam)
{
    if (e == G.nvars) snprintf(buf, tam, "qF");
    else snprintf(buf, tam, "%c", G.vars[e]);
}

/* Nome do conjunto */
static void nomeConjunto(Conjunto c, char *buf, size_t tam)
{
    int e, primeiro = 1;
    char nome[8];
    buf[0] = '\0';
    anexar(buf, tam, "{");
    for (e = 0; e < N.n; e++) {
        if (!(c & (1UL << e))) continue;
        if (!primeiro) anexar(buf, tam, ",");
        nomeEstadoAFN(e, nome, sizeof nome);
        anexar(buf, tam, nome);
        primeiro = 0;
    }
    anexar(buf, tam, "}");
}

/* Descricao do conjunto */
static void descreverConjunto(Conjunto c, int morto, char *buf, size_t tam)
{
    int e, primeiro = 1;
    buf[0] = '\0';
    if (morto) {
        anexar(buf, tam, "estado morto (nao ha como aceitar)");
        return;
    }
    for (e = 0; e < N.n; e++) {
        const char *d;
        char nome[8];
        if (!(c & (1UL << e))) continue;
        d = (e == G.nvars) ? G.descFinal : G.desc[(unsigned char)G.vars[e]];
        if (d[0] == '\0') {
            nomeEstadoAFN(e, nome, sizeof nome);
            d = nome;
        }
        if (!primeiro) anexar(buf, tam, " / ");
        anexar(buf, tam, d);
        primeiro = 0;
    }
}

/* Monta o AFN */
static void construirAFN(void)
{
    int i, qF = G.nvars;

    memset(&N, 0, sizeof N);
    N.n = G.nvars + 1;
    N.finais = 1UL << qF;

    for (i = 0; i < G.nprods; i++) {
        const Producao *p = &G.prods[i];
        if (p->term < 0)
            N.finais |= 1UL << p->lhs;                         /* X -> &  */
        else if (p->rhs >= 0)
            N.delta[p->lhs][p->term] |= 1UL << p->rhs;         /* X -> aY */
        else
            N.delta[p->lhs][p->term] |= 1UL << qF;             /* X -> a  */
    }
}

/* Mostra o AFN */
static void imprimirAFN(void)
{
    int e, t, larg = 6;
    char buf[MAX_NOME];

    for (e = 0; e < N.n; e++)
        for (t = 0; t < G.nterms; t++) {
            nomeConjunto(N.delta[e][t], buf, sizeof buf);
            if ((int)strlen(buf) + 2 > larg) larg = (int)strlen(buf) + 2;
        }

    printf("\n=====================================================\n");
    printf(" 2. AFN CONSTRUIDO A PARTIR DA GR\n");
    printf("    (-> = inicial, * = final, {} = sem transicao)\n");
    printf("=====================================================\n");
    printf("%-8s", "delta");
    for (t = 0; t < G.nterms; t++) printf("%-*c", larg, G.terms[t]);
    printf("\n");

    for (e = 0; e < N.n; e++) {
        char nome[8], rotulo[16];
        nomeEstadoAFN(e, nome, sizeof nome);
        snprintf(rotulo, sizeof rotulo, "%s%s%s", e == 0 ? "->" : "  ",
                 (N.finais & (1UL << e)) ? "*" : " ", nome);
        printf("%-8s", rotulo);
        for (t = 0; t < G.nterms; t++) {
            nomeConjunto(N.delta[e][t], buf, sizeof buf);
            printf("%-*s", larg, buf);
        }
        printf("\n");
    }
}

/* ===== 3. AFN -> AFD ===== */

/* Acha estados mortos */
static void calcularMortos(AFD *a)
{
    int i, t, mudou;
    int vivo[MAX_AFD];

    for (i = 0; i < a->n; i++) vivo[i] = a->final[i];
    do {
        mudou = 0;
        for (i = 0; i < a->n; i++) {
            if (vivo[i]) continue;
            for (t = 0; t < G.nterms; t++)
                if (vivo[a->delta[i][t]]) { vivo[i] = 1; mudou = 1; break; }
        }
    } while (mudou);

    for (i = 0; i < a->n; i++) a->morto[i] = !vivo[i];
}

/* Subconjuntos */
static void construirAFD(void)
{
    int i, j, t, e;

    memset(&D, 0, sizeof D);
    D.conj[0] = 1UL << 0;   /* comeca em {S} */
    D.n = 1;

    for (i = 0; i < D.n; i++) {
        for (t = 0; t < G.nterms; t++) {
            Conjunto destino = 0;
            for (e = 0; e < N.n; e++)
                if (D.conj[i] & (1UL << e)) destino |= N.delta[e][t];

            /* novo conjunto = novo estado */
            for (j = 0; j < D.n; j++)
                if (D.conj[j] == destino) break;
            if (j == D.n) {
                if (D.n == MAX_AFD) erro("o AFD passou do limite de estados", 0);
                D.conj[D.n++] = destino;
            }
            D.delta[i][t] = j;
        }
        D.final[i] = (D.conj[i] & N.finais) != 0;
    }
    calcularMortos(&D);
}

/* Mostra o AFD */
static void imprimirAFD(void)
{
    int i, t;
    char buf[MAX_NOME], rotulo[16];

    printf("\n=====================================================\n");
    printf(" 3. AFD (construcao de subconjuntos) - %d estados\n", D.n);
    printf("=====================================================\n");
    for (i = 0; i < D.n; i++) {
        nomeConjunto(D.conj[i], buf, sizeof buf);
        printf("  d%-3d = %-10s%s%s\n", i, buf, D.final[i] ? " (final)" : "",
               D.morto[i] ? " (morto)" : "");
    }

    printf("\n%-8s", "delta");
    for (t = 0; t < G.nterms; t++) printf("%-6c", G.terms[t]);
    printf("\n");
    for (i = 0; i < D.n; i++) {
        snprintf(rotulo, sizeof rotulo, "%s%sd%d", i == 0 ? "->" : "  ",
                 D.final[i] ? "*" : " ", i);
        printf("%-8s", rotulo);
        for (t = 0; t < G.nterms; t++) {
            snprintf(buf, sizeof buf, "d%d", D.delta[i][t]);
            printf("%-6s", buf);
        }
        printf("\n");
    }
}

/* ===== 4. Minimizacao ===== */

/* Par marcado? */
static int parMarcado(int a, int b)
{
    return a < b ? marcado[a][b] : marcado[b][a];
}

static void minimizar(void)
{
    int i, j, t, mudou, c;

    /* 1: final x nao final */
    memset(marcado, 0, sizeof marcado);
    for (i = 0; i < D.n; i++)
        for (j = i + 1; j < D.n; j++)
            marcado[i][j] = (D.final[i] != D.final[j]);

    /* 2: marca ate nao mudar */
    do {
        mudou = 0;
        for (i = 0; i < D.n; i++)
            for (j = i + 1; j < D.n; j++) {
                if (marcado[i][j]) continue;
                for (t = 0; t < G.nterms; t++) {
                    int a = D.delta[i][t], b = D.delta[j][t];
                    if (a != b && parMarcado(a, b)) {
                        marcado[i][j] = 1;
                        mudou = 1;
                        break;
                    }
                }
            }
    } while (mudou);

    /* 3: junta os iguais */
    memset(&M, 0, sizeof M);
    for (i = 0; i < D.n; i++) classeDe[i] = -1;
    for (i = 0; i < D.n; i++) {
        if (classeDe[i] != -1) continue;
        c = M.n++;
        for (j = i; j < D.n; j++)
            if (classeDe[j] == -1 && (j == i || !marcado[i][j])) {
                classeDe[j] = c;
                M.conj[c] |= D.conj[j];
            }
    }

    /* transicoes */
    for (i = 0; i < D.n; i++) {
        c = classeDe[i];
        M.final[c] = D.final[i];
        for (t = 0; t < G.nterms; t++)
            M.delta[c][t] = classeDe[D.delta[i][t]];
    }
    calcularMortos(&M);

    for (c = 0; c < M.n; c++)
        descreverConjunto(M.conj[c], M.morto[c], descMin[c], sizeof descMin[c]);
}

/* Mostra a minimizacao */
static void imprimirMinimizacao(void)
{
    int i, j, c, houveFusao = 0;
    char buf[MAX_NOME];

    printf("\n=====================================================\n");
    printf(" 4. MINIMIZACAO (tabela de marcacao de pares)\n");
    printf("    x = distinguiveis    = = equivalentes\n");
    printf("=====================================================\n");

    if (D.n <= 20) {
        for (i = 1; i < D.n; i++) {
            printf("  d%-3d", i);
            for (j = 0; j < i; j++) printf(" %-4s", marcado[j][i] ? "x" : "=");
            printf("\n");
        }
        printf("      ");
        for (j = 0; j < D.n - 1; j++) {
            snprintf(buf, sizeof buf, "d%d", j);
            printf(" %-4s", buf);
        }
        printf("\n");
    } else {
        printf("  (tabela omitida: o AFD tem mais de 20 estados)\n");
    }

    printf("\nEstados equivalentes encontrados:\n");
    for (c = 0; c < M.n; c++) {
        int qtd = 0;
        for (i = 0; i < D.n; i++) if (classeDe[i] == c) qtd++;
        if (qtd < 2) continue;
        houveFusao = 1;
        printf("  ");
        for (i = 0, j = 0; i < D.n; i++) {
            if (classeDe[i] != c) continue;
            nomeConjunto(D.conj[i], buf, sizeof buf);
            printf("%sd%d %s", j++ ? "  ==  " : "", i, buf);
        }
        printf("   -> fundidos em q%d\n", c);
    }
    if (!houveFusao) printf("  nenhum: o AFD ja era minimo.\n");
}

/* Mostra o AFD minimo */
static void imprimirMinimo(void)
{
    int c, t, i;
    char buf[MAX_NOME], rotulo[16];

    printf("\n=====================================================\n");
    printf(" 5. AFD MINIMO - %d estados (antes: %d)\n", M.n, D.n);
    printf("=====================================================\n");

    printf("Q  = {");
    for (c = 0; c < M.n; c++) printf("%sq%d", c ? ", " : "", c);
    printf("}\nS  = {");
    for (t = 0; t < G.nterms; t++) printf("%s%c", t ? ", " : "", G.terms[t]);
    printf("}\nq0 = estado inicial\nF  = {");
    for (c = 0, i = 0; c < M.n; c++)
        if (M.final[c]) printf("%sq%d", i++ ? ", " : "", c);
    printf("}\n\n");

    for (c = 0; c < M.n; c++) {
        char comp[MAX_NOME] = "";
        for (i = 0; i < D.n; i++) {
            if (classeDe[i] != c) continue;
            if (comp[0]) anexar(comp, sizeof comp, " + ");
            snprintf(buf, sizeof buf, "d%d", i);
            anexar(comp, sizeof comp, buf);
        }
        printf("  q%-3d = %-10s %s\n", c, comp, descMin[c]);
    }

    printf("\nFuncao de transicao delta:\n%-8s", "delta");
    for (t = 0; t < G.nterms; t++) printf("%-6c", G.terms[t]);
    printf("\n");
    for (c = 0; c < M.n; c++) {
        snprintf(rotulo, sizeof rotulo, "%s%sq%d", c == 0 ? "->" : "  ",
                 M.final[c] ? "*" : " ", c);
        printf("%-8s", rotulo);
        for (t = 0; t < G.nterms; t++) {
            snprintf(buf, sizeof buf, "q%d", M.delta[c][t]);
            printf("%-6s", buf);
        }
        printf("\n");
    }

    printf("\nLista de transicoes (sem o estado morto):\n");
    for (c = 0; c < M.n; c++) {
        if (M.morto[c]) continue;
        for (t = 0; t < G.nterms; t++)
            if (!M.morto[M.delta[c][t]])
                printf("  delta(q%d, %c) = q%d\n", c, G.terms[t], M.delta[c][t]);
    }
}

/* ===== 5. JFLAP ===== */

/* Texto para XML */
static void escreverXML(FILE *f, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        default: fputc(*s, f);
        }
    }
}

/* Gera o .jff */
static void exportarJFLAP(const char *arquivo)
{
    int c, t, pos = 0;
    FILE *f = fopen(arquivo, "w");
    if (f == NULL) {
        fprintf(stderr, "Aviso: nao foi possivel criar %s\n", arquivo);
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>"
               "<!--Created with afd_minimo.c--><structure>\n");
    fprintf(f, "\t<type>fa</type>\n\t<automaton>\n");

    /* estados */
    for (c = 0; c < M.n; c++) {
        if (M.morto[c]) continue;
        fprintf(f, "\t\t<state id=\"%d\" name=\"q%d\">\n", c, c);
        fprintf(f, "\t\t\t<x>%d.0</x>\n\t\t\t<y>%d.0</y>\n",
                100 + 160 * pos, (pos % 2) ? 300 : 150);
        fprintf(f, "\t\t\t<label>");
        escreverXML(f, descMin[c]);
        fprintf(f, "</label>\n");
        if (c == 0) fprintf(f, "\t\t\t<initial/>\n");
        if (M.final[c]) fprintf(f, "\t\t\t<final/>\n");
        fprintf(f, "\t\t</state>\n");
        pos++;
    }

    /* transicoes */
    for (c = 0; c < M.n; c++) {
        if (M.morto[c]) continue;
        for (t = 0; t < G.nterms; t++) {
            int d = M.delta[c][t];
            if (M.morto[d]) continue;
            fprintf(f, "\t\t<transition>\n\t\t\t<from>%d</from>\n\t\t\t<to>%d</to>\n"
                       "\t\t\t<read>%c</read>\n\t\t</transition>\n", c, d, G.terms[t]);
        }
    }
    fprintf(f, "\t</automaton>\n</structure>\n");
    fclose(f);
    printf("\nAFD minimo exportado para o JFLAP: %s\n", arquivo);
}

/* ===== 6. Testar palavra ===== */

static void simular(const char *palavra)
{
    int q = 0, passo = 0;
    const char *s;

    printf("\nPalavra \"%s\"\n", palavra);
    printf("  inicio em q0 (%s)\n", descMin[0]);

    for (s = palavra; *s; s++) {
        int t, prox;
        char ch = *s;
        if (isspace((unsigned char)ch) || ch == '&') continue;

        t = indiceTerm(ch, 0);
        if (t < 0) {
            printf("  '%c' nao pertence ao alfabeto -> palavra REJEITADA\n", ch);
            return;
        }
        prox = M.delta[q][t];
        printf("  %2d. le '%c' (%s): q%d -> q%d (%s)\n", ++passo, ch,
               G.desc[(unsigned char)ch][0] ? G.desc[(unsigned char)ch] : "-",
               q, prox, descMin[prox]);
        q = prox;
        if (M.morto[q]) {
            printf("  Entrou no estado morto -> palavra REJEITADA\n");
            return;
        }
    }

    if (M.final[q])
        printf("  Resultado: ACEITA (terminou em q%d: %s)\n", q, descMin[q]);
    else
        printf("  Resultado: REJEITADA (terminou em q%d, que nao e final: %s)\n",
               q, descMin[q]);
}

/* ===== Main ===== */

int main(int argc, char *argv[])
{
    const char *arquivo = NULL;
    int i, palavrasNaLinha = 0;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            arquivo = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("Uso: %s [-g gramatica.txt] [palavra ...]\n", argv[0]);
            return 0;
        } else {
            palavrasNaLinha++;
        }
    }

    carregarGramatica(arquivo);
    imprimirGramatica();
    construirAFN();
    imprimirAFN();
    construirAFD();
    imprimirAFD();
    minimizar();
    imprimirMinimizacao();
    imprimirMinimo();
    exportarJFLAP("afd_minimo.jff");

    printf("\n=====================================================\n");
    printf(" 6. SIMULACAO DO AFD MINIMO\n");
    printf("=====================================================\n");

    if (palavrasNaLinha > 0) {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-g") == 0) { i++; continue; }
            simular(argv[i]);
        }
    } else {
        char linha[MAX_LINHA];
        printf("Digite uma palavra (use & para a palavra vazia; Enter vazio sai).\n");
        for (;;) {
            char *p;
            printf("\n> ");
            fflush(stdout);
            if (fgets(linha, sizeof linha, stdin) == NULL) break;
            p = aparar(linha);
            if (*p == '\0') break;
            simular(p);
        }
    }
    return 0;
}
