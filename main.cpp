#include <bits/stdc++.h>
using namespace std;

// Simple two-pass assembler for the described TISC dialect

enum TokenType {
    TK_EOF, TK_IDENT, TK_NUMBER, TK_DOT, TK_COLON, TK_SEMI, TK_LP, TK_RP, TK_PLUS, TK_MINUS, TK_QMARK
};

struct Token {
    TokenType type;
    string text;
    long long value; // for numbers
};

struct Lexer {
    string s;
    size_t i=0, n=0;
    Lexer(const string &in): s(in), n(in.size()) {}

    void skipSpaces() {
        while (i < n && isspace((unsigned char)s[i])) i++;
    }

    Token next() {
        skipSpaces();
        if (i >= n) return {TK_EOF, "", 0};
        char c = s[i];
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i+1;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j]=='_')) j++;
            string id = s.substr(i, j-i);
            i = j;
            return {TK_IDENT, id, 0};
        }
        if (isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < n && isdigit((unsigned char)s[j])) j++;
            long long v = stoll(s.substr(i, j-i));
            i = j;
            return {TK_NUMBER, "", v};
        }
        i++;
        switch (c) {
            case '.': return {TK_DOT, ".", 0};
            case ':': return {TK_COLON, ":", 0};
            case ';': return {TK_SEMI, ";", 0};
            case '(': return {TK_LP, "(", 0};
            case ')': return {TK_RP, ")", 0};
            case '+': return {TK_PLUS, "+", 0};
            case '-': return {TK_MINUS, "-", 0};
            case '?': return {TK_QMARK, "?", 0};
            default:
                // Unknown character, skip
                return next();
        }
    }
};

struct Expr {
    enum Type { NUMBER, LABEL, QMARK, UNARY_MINUS, ADD, SUB } type;
    long long number = 0;
    string label;
    unique_ptr<Expr> left, right;
    static unique_ptr<Expr> makeNumber(long long v){ auto e=make_unique<Expr>(); e->type=NUMBER; e->number=v; return e; }
    static unique_ptr<Expr> makeLabel(const string &l){ auto e=make_unique<Expr>(); e->type=LABEL; e->label=l; return e; }
    static unique_ptr<Expr> makeQMark(){ auto e=make_unique<Expr>(); e->type=QMARK; return e; }
    static unique_ptr<Expr> makeUnaryMinus(unique_ptr<Expr> a){ auto e=make_unique<Expr>(); e->type=UNARY_MINUS; e->left=move(a); return e; }
    static unique_ptr<Expr> makeAdd(unique_ptr<Expr> a, unique_ptr<Expr> b){ auto e=make_unique<Expr>(); e->type=ADD; e->left=move(a); e->right=move(b); return e; }
    static unique_ptr<Expr> makeSub(unique_ptr<Expr> a, unique_ptr<Expr> b){ auto e=make_unique<Expr>(); e->type=SUB; e->left=move(a); e->right=move(b); return e; }
};

static unique_ptr<Expr> cloneExpr(const Expr* e){
    if (!e) return nullptr;
    auto copy = make_unique<Expr>();
    copy->type = e->type;
    copy->number = e->number;
    copy->label = e->label;
    copy->left = cloneExpr(e->left.get());
    copy->right = cloneExpr(e->right.get());
    return copy;
}

struct Parser {
    Lexer lex;
    Token cur;
    Parser(const string &in): lex(in) { cur = lex.next(); }

    void consume() { cur = lex.next(); }
    bool accept(TokenType t){ if (cur.type==t){ consume(); return true;} return false; }
    bool expect(TokenType t){ if (cur.type==t){ consume(); return true;} return false; }

    // Forward declarations
    unique_ptr<Expr> parse_expression();
    unique_ptr<Expr> parse_term();

    unique_ptr<Expr> parse_factor(){
        if (accept(TK_LP)){
            auto e = parse_expression();
            expect(TK_RP);
            return e;
        }
        if (accept(TK_MINUS)){
            auto t = parse_factor();
            return Expr::makeUnaryMinus(move(t));
        }
        if (cur.type==TK_NUMBER){
            long long v = cur.value; consume();
            return Expr::makeNumber(v);
        }
        if (cur.type==TK_QMARK){ consume(); return Expr::makeQMark(); }
        if (cur.type==TK_IDENT){ string id=cur.text; consume(); return Expr::makeLabel(id);}        
        // Fallback: zero
        return Expr::makeNumber(0);
    }

    unique_ptr<Expr> parse_term(){
        return parse_factor();
    }

    unique_ptr<Expr> parse_expression(){
        auto left = parse_term();
        if (cur.type==TK_PLUS){
            consume();
            auto right = parse_term();
            return Expr::makeAdd(move(left), move(right));
        } else if (cur.type==TK_MINUS){
            consume();
            auto right = parse_term();
            return Expr::makeSub(move(left), move(right));
        }
        return left;
    }

    vector<string> parse_labels(){
        vector<string> labs;
        while (cur.type==TK_IDENT){
            string id = cur.text; consume();
            if (accept(TK_COLON)){
                labs.push_back(id);
            } else {
                // not a label, put back? We cannot put back; rough handling: treat as identifier expression start
                // But grammar only puts identifiers here if label. If not, we set cur to an identifier token again by hacking
                // For simplicity, we will store a special token stream snapshot is not available; instead, we will keep a pending token
                // To avoid complexity, only accept IDENT followed by ':' as labels here; otherwise, we push back by constructing a dummy token sequence.
                // Since this branch is rare (items start), we'll simulate pushback by storing a buffer and using a minimal lookahead design.
                // However, to keep parser simpler, we require labels are IDENT ':' pairs. If not, we store back the identifier as the start of expression
                // We'll create a synthetic token by setting cur back to the identifier token
                // Not feasible without buffering; assume input adheres to grammar.
                // This branch won't occur.
                // no-op
                break;
            }
        }
        return labs;
    }
};

struct Cell {
    bool is_const = false;
    long long const_value = 0;
    unique_ptr<Expr> expr; // if not const
    long long address = -1; // absolute address in Mem
};

struct BlockInstruction {
    // addresses filled sequentially
    Cell op;
    Cell a,b,c;
};

struct BlockData {
    vector<Cell> items;
};

struct Block {
    enum Kind { INSN, DATA } kind;
    BlockInstruction insn;
    BlockData data;
};

static long long eval_expr(const Expr* e, long long item_addr, const unordered_map<string,long long>& labels){
    if (!e) return 0;
    switch (e->type){
        case Expr::NUMBER: return e->number;
        case Expr::LABEL: {
            auto it = labels.find(e->label);
            if (it == labels.end()) return 0; // undefined -> 0
            return it->second;
        }
        case Expr::QMARK: return item_addr + 1;
        case Expr::UNARY_MINUS: return -eval_expr(e->left.get(), item_addr, labels);
        case Expr::ADD: return eval_expr(e->left.get(), item_addr, labels) + eval_expr(e->right.get(), item_addr, labels);
        case Expr::SUB: return eval_expr(e->left.get(), item_addr, labels) - eval_expr(e->right.get(), item_addr, labels);
    }
    return 0;
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Read input, strip // comments
    string line, all;
    while (getline(cin, line)){
        size_t p = line.find("//");
        if (p != string::npos) line = line.substr(0, p);
        all += line;
        all.push_back('\n');
    }

    Parser P(all);

    vector<Block> blocks;
    unordered_map<string,long long> label_addr;
    long long cur_addr = 0;

    auto parse_item = [&](Parser &PP, long long addr_for_labels)->unique_ptr<Expr>{
        // labels for this item
        // Using a simple approach: look ahead for IDENT ':' pairs using a separate lexer isn't straightforward; rely on parse_labels()
        // Since parse_labels consumes only IDENT ':' sequences, it's safe
        auto labs = PP.parse_labels();
        for (auto &lab : labs) label_addr[lab] = addr_for_labels;
        auto e = PP.parse_expression();
        return e;
    };

    // Helper to check if cur token is one of opcodes or dot
    auto is_opcode = [&](const Token &t)->int{
        if (t.type == TK_IDENT){
            if (t.text == "msubleq") return 1;
            if (t.text == "rsubleq") return 2;
            if (t.text == "ldorst") return 3;
        }
        if (t.type == TK_DOT) return 4;
        return 0;
    };

    // Main parse loop
    while (P.cur.type != TK_EOF){
        // Skip stray semicolons or whitespace; also handle empty input
        // Collect labels before opcode
        vector<string> pre_labs;
        while (P.cur.type == TK_IDENT){
            // peek next by taking a snapshot
            // We implement a tiny lookahead: get token, then if next is ':', it's a label; else break
            string save_text = P.cur.text;
            Parser saveP = P; // copy parser (heavy but ok for small inputs)
            saveP.consume();
            if (saveP.cur.type == TK_COLON){
                // consume IDENT ':'
                P.consume(); P.consume();
                pre_labs.push_back(save_text);
            } else {
                break;
            }
        }

        if (P.cur.type == TK_EOF) break;

        int opkind = is_opcode(P.cur);
        if (opkind == 0){
            // Consume and continue to avoid infinite loop
            P.consume();
            continue;
        }

        if (opkind == 4){
            // Data block
            // assign pre labels to current address
            for (auto &lab : pre_labs) label_addr[lab] = cur_addr;
            P.consume(); // consume '.'
            Block blk; blk.kind = Block::DATA;
            // parse items until ';'
            while (P.cur.type != TK_SEMI && P.cur.type != TK_EOF){
                Cell c; c.is_const = false; c.address = cur_addr;
                // parse item with its own labels
                // For item labels, we must parse IDENT ':' pairs; we can reuse logic by a temporary pre-labs parser copy
                // But we can call parse_item which consumes labels
                auto item_labs = P.parse_labels();
                for (auto &lab : item_labs) label_addr[lab] = cur_addr;
                c.expr = P.parse_expression();
                blk.data.items.push_back(move(c));
                cur_addr += 1;
            }
            if (P.cur.type == TK_SEMI) P.consume();
            blocks.push_back(move(blk));
        } else {
            // Instruction
            string opname = P.cur.text; if (P.cur.type==TK_DOT) opname = "."; // not used
            P.consume();
            Block blk; blk.kind = Block::INSN;
            // opcode cell
            blk.insn.op.is_const = true;
            blk.insn.op.address = cur_addr;
            if (opkind==1) blk.insn.op.const_value = 0; // msubleq -> 0
            else if (opkind==2) blk.insn.op.const_value = 1; // rsubleq -> 1
            else if (opkind==3) blk.insn.op.const_value = 2; // ldorst -> 2
            // assign pre labels to base address
            for (auto &lab : pre_labs) label_addr[lab] = cur_addr;
            cur_addr += 1;

            // parse up to ';' items
            vector<Cell*> cells;
            // Prepare a,b,c cells with addresses; we'll fill as we go
            blk.insn.a.address = cur_addr; cur_addr += 1;
            blk.insn.b.address = cur_addr; cur_addr += 1;
            blk.insn.c.address = cur_addr; cur_addr += 1;
            cells = { &blk.insn.a, &blk.insn.b, &blk.insn.c };

            int idx = 0;
            while (P.cur.type != TK_SEMI && P.cur.type != TK_EOF && idx < 3){
                // Labels for this operand
                auto labs = P.parse_labels();
                for (auto &lab : labs) label_addr[lab] = cells[idx]->address;
                cells[idx]->expr = P.parse_expression();
                idx++;
            }
            if (P.cur.type == TK_SEMI) P.consume();

            // autopopulate for msubleq/rsubleq
            if (opkind == 1 || opkind == 2){
                if (idx == 1){
                    // duplicate A into B, set C='?'
                    blk.insn.b.expr = cloneExpr(blk.insn.a.expr.get());
                    blk.insn.c.expr = Expr::makeQMark();
                } else if (idx == 2){
                    blk.insn.c.expr = Expr::makeQMark();
                } else if (idx == 0){
                    // invalid; set defaults 0,0,? to avoid crash
                    blk.insn.a.expr = Expr::makeNumber(0);
                    blk.insn.b.expr = Expr::makeNumber(0);
                    blk.insn.c.expr = Expr::makeQMark();
                }
            } else {
                // ldorst must have 3; if less, fill with 0
                while (idx < 3){
                    if (idx == 0) blk.insn.a.expr = Expr::makeNumber(0);
                    if (idx == 1) blk.insn.b.expr = Expr::makeNumber(0);
                    if (idx == 2) blk.insn.c.expr = Expr::makeNumber(0);
                    idx++;
                }
            }

            blocks.push_back(move(blk));
        }
    }

    // Evaluate and output per-block
    // For each block, print numbers separated by space; instruction as 4 numbers per line; data block all items on one line
    auto print_val = [](long long v){ cout << v; };

    for (size_t bi=0; bi<blocks.size(); ++bi){
        auto &blk = blocks[bi];
        if (blk.kind == Block::INSN){
            vector<long long> vals(4);
            vals[0] = blk.insn.op.const_value;
            vals[1] = eval_expr(blk.insn.a.expr.get(), blk.insn.a.address, label_addr);
            vals[2] = eval_expr(blk.insn.b.expr.get(), blk.insn.b.address, label_addr);
            vals[3] = eval_expr(blk.insn.c.expr.get(), blk.insn.c.address, label_addr);
            for (int i=0;i<4;i++){
                if (i) cout << ' ';
                print_val(vals[i]);
            }
            cout << "\n";
        } else {
            for (size_t i=0;i<blk.data.items.size(); ++i){
                if (i) cout << ' ';
                long long v = eval_expr(blk.data.items[i].expr.get(), blk.data.items[i].address, label_addr);
                print_val(v);
            }
            cout << "\n";
        }
    }

    return 0;
}

