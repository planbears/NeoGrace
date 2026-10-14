#include "expr.h"
#include "i18n.h"

#include <QHash>
#include <QRegularExpression>
#include <cmath>
#include <limits>
#include <math.h>
#include <random>

namespace ng {

static const double NaN = std::numeric_limits<double>::quiet_NaN();

enum class NK { Num, Var, Param, Ref, Func, Neg, Not, Bin, Tern };

struct ExprNode {
    NK kind = NK::Num;
    double num = 0;
    int slot = 0;  // Var: 0..5 столбцы, 10 i, 11 n, 12 $t
    QString ref;
    int op = 0;
    int fn = 0;
    std::vector<std::unique_ptr<ExprNode>> kids;
};

namespace {

std::mt19937_64 &rng()
{
    static std::mt19937_64 g(12345);
    return g;
}

struct FuncDef {
    const char *name;
    int arity;  // -1 — переменное (min/max)
    int id;
};

enum FnId {
    F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_ATAN2, F_SINH, F_COSH, F_TANH,
    F_ASINH, F_ACOSH, F_ATANH, F_EXP, F_LN, F_LOG10, F_LOG2, F_SQRT, F_CBRT, F_ABS,
    F_SIGN, F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_MIN, F_MAX, F_POW, F_MOD, F_HYPOT,
    F_ERF, F_ERFC, F_GAMMA, F_LGAMMA, F_J0, F_J1, F_Y0, F_Y1, F_RAND, F_RNORM,
    F_DEG, F_RAD, F_HEAV, F_ISNAN, F_SQR, F_FAC, F_IRAND, F_NORMPDF,
};

const FuncDef kFuncs[] = {
    {"sin", 1, F_SIN}, {"cos", 1, F_COS}, {"tan", 1, F_TAN}, {"asin", 1, F_ASIN},
    {"arcsin", 1, F_ASIN}, {"acos", 1, F_ACOS}, {"arccos", 1, F_ACOS}, {"atan", 1, F_ATAN},
    {"arctan", 1, F_ATAN}, {"atan2", 2, F_ATAN2}, {"sinh", 1, F_SINH}, {"cosh", 1, F_COSH},
    {"tanh", 1, F_TANH}, {"asinh", 1, F_ASINH}, {"acosh", 1, F_ACOSH}, {"atanh", 1, F_ATANH},
    {"exp", 1, F_EXP}, {"ln", 1, F_LN}, {"log", 1, F_LN}, {"log10", 1, F_LOG10},
    {"lg", 1, F_LOG10}, {"log2", 1, F_LOG2}, {"sqrt", 1, F_SQRT}, {"cbrt", 1, F_CBRT},
    {"abs", 1, F_ABS}, {"fabs", 1, F_ABS}, {"sign", 1, F_SIGN}, {"sgn", 1, F_SIGN},
    {"floor", 1, F_FLOOR}, {"ceil", 1, F_CEIL}, {"round", 1, F_ROUND}, {"rint", 1, F_ROUND},
    {"int", 1, F_TRUNC}, {"trunc", 1, F_TRUNC}, {"min", -1, F_MIN}, {"max", -1, F_MAX},
    {"pow", 2, F_POW}, {"mod", 2, F_MOD}, {"fmod", 2, F_MOD}, {"hypot", 2, F_HYPOT},
    {"erf", 1, F_ERF}, {"erfc", 1, F_ERFC}, {"gamma", 1, F_GAMMA}, {"lgamma", 1, F_LGAMMA},
    {"j0", 1, F_J0}, {"j1", 1, F_J1}, {"y0", 1, F_Y0}, {"y1", 1, F_Y1},
    {"rand", 0, F_RAND}, {"rnd", 0, F_RAND}, {"rnorm", 2, F_RNORM}, {"irand", 1, F_IRAND},
    {"deg", 1, F_DEG}, {"rad", 1, F_RAD}, {"heaviside", 1, F_HEAV}, {"isnan", 1, F_ISNAN},
    {"sqr", 1, F_SQR}, {"fac", 1, F_FAC}, {"normpdf", 1, F_NORMPDF},
};

enum Ops { O_ADD, O_SUB, O_MUL, O_DIV, O_POW, O_MODOP, O_LT, O_GT, O_LE, O_GE, O_EQ, O_NE, O_AND, O_OR };

struct Tok {
    enum T { End, Num, Id, Op, LP, RP, Comma, Q, Colon } t = End;
    double num = 0;
    QString s;
};

class Lexer {
public:
    explicit Lexer(const QString &src) : s(src) {}
    QString s;
    int pos = 0;
    Tok next()
    {
        while (pos < s.size() && s[pos].isSpace())
            ++pos;
        Tok t;
        if (pos >= s.size())
            return t;
        QChar c = s[pos];
        if (c.isDigit() || (c == '.' && pos + 1 < s.size() && s[pos + 1].isDigit())) {
            int st = pos;
            while (pos < s.size() && (s[pos].isDigit() || s[pos] == '.'))
                ++pos;
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                int save = pos;
                ++pos;
                if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
                    ++pos;
                if (pos < s.size() && s[pos].isDigit()) {
                    while (pos < s.size() && s[pos].isDigit())
                        ++pos;
                } else {
                    pos = save;
                }
            }
            t.t = Tok::Num;
            t.num = s.mid(st, pos - st).toDouble();
            return t;
        }
        if (c.isLetter() || c == '_' || c == '$') {
            int st = pos;
            while (pos < s.size() && (s[pos].isLetterOrNumber() || s[pos] == '_' || s[pos] == '$' || s[pos] == '.'))
                ++pos;
            t.t = Tok::Id;
            t.s = s.mid(st, pos - st).toLower();
            return t;
        }
        ++pos;
        auto two = [&](QChar n) {
            if (pos < s.size() && s[pos] == n) {
                ++pos;
                return true;
            }
            return false;
        };
        switch (c.toLatin1()) {
        case '(': t.t = Tok::LP; return t;
        case ')': t.t = Tok::RP; return t;
        case ',': t.t = Tok::Comma; return t;
        case '?': t.t = Tok::Q; return t;
        case ':': t.t = Tok::Colon; return t;
        case '+': t.t = Tok::Op; t.s = "+"; return t;
        case '-': t.t = Tok::Op; t.s = "-"; return t;
        case '*': t.t = Tok::Op; t.s = two('*') ? "^" : "*"; return t;
        case '/': t.t = Tok::Op; t.s = "/"; return t;
        case '^': t.t = Tok::Op; t.s = "^"; return t;
        case '%': t.t = Tok::Op; t.s = "%"; return t;
        case '<': t.t = Tok::Op; t.s = two('=') ? "<=" : "<"; return t;
        case '>': t.t = Tok::Op; t.s = two('=') ? ">=" : ">"; return t;
        case '=': t.t = Tok::Op; two('='); t.s = "=="; return t;
        case '!': t.t = Tok::Op; t.s = two('=') ? "!=" : "!"; return t;
        case '&': t.t = Tok::Op; two('&'); t.s = "&&"; return t;
        case '|': t.t = Tok::Op; two('|'); t.s = "||"; return t;
        default:
            t.t = Tok::Op;
            t.s = QString(c);
            return t;
        }
    }
};

class ParserX {
public:
    ParserX(const QString &src, QStringList &vars) : lx(src), vars(vars) { adv(); }
    Lexer lx;
    Tok cur;
    QString err;
    QStringList &vars;

    void adv() { cur = lx.next(); }
    bool isOp(const char *o) const { return cur.t == Tok::Op && cur.s == QLatin1String(o); }

    using P = std::unique_ptr<ExprNode>;
    P mk(NK k)
    {
        auto n = std::make_unique<ExprNode>();
        n->kind = k;
        return n;
    }
    P bin(int op, P a, P b)
    {
        auto n = mk(NK::Bin);
        n->op = op;
        n->kids.push_back(std::move(a));
        n->kids.push_back(std::move(b));
        return n;
    }

    P expr()
    {
        P c = orE();
        if (!c)
            return nullptr;
        if (cur.t == Tok::Q) {
            adv();
            P a = expr();
            if (!a)
                return nullptr;
            if (cur.t != Tok::Colon) {
                err = T("ожидалось «:»");
                return nullptr;
            }
            adv();
            P b = expr();
            if (!b)
                return nullptr;
            auto n = mk(NK::Tern);
            n->kids.push_back(std::move(c));
            n->kids.push_back(std::move(a));
            n->kids.push_back(std::move(b));
            return n;
        }
        return c;
    }
    P orE()
    {
        P a = andE();
        while (a && (isOp("||") || (cur.t == Tok::Id && cur.s == "or"))) {
            adv();
            P b = andE();
            if (!b)
                return nullptr;
            a = bin(O_OR, std::move(a), std::move(b));
        }
        return a;
    }
    P andE()
    {
        P a = cmpE();
        while (a && (isOp("&&") || (cur.t == Tok::Id && cur.s == "and"))) {
            adv();
            P b = cmpE();
            if (!b)
                return nullptr;
            a = bin(O_AND, std::move(a), std::move(b));
        }
        return a;
    }
    P cmpE()
    {
        P a = addE();
        while (a && cur.t == Tok::Op) {
            int op = -1;
            if (cur.s == "<") op = O_LT;
            else if (cur.s == ">") op = O_GT;
            else if (cur.s == "<=") op = O_LE;
            else if (cur.s == ">=") op = O_GE;
            else if (cur.s == "==") op = O_EQ;
            else if (cur.s == "!=") op = O_NE;
            if (op < 0)
                break;
            adv();
            P b = addE();
            if (!b)
                return nullptr;
            a = bin(op, std::move(a), std::move(b));
        }
        return a;
    }
    P addE()
    {
        P a = mulE();
        while (a && (isOp("+") || isOp("-"))) {
            int op = cur.s == "+" ? O_ADD : O_SUB;
            adv();
            P b = mulE();
            if (!b)
                return nullptr;
            a = bin(op, std::move(a), std::move(b));
        }
        return a;
    }
    P mulE()
    {
        P a = unary();
        while (a && (isOp("*") || isOp("/") || isOp("%"))) {
            int op = cur.s == "*" ? O_MUL : cur.s == "/" ? O_DIV : O_MODOP;
            adv();
            P b = unary();
            if (!b)
                return nullptr;
            a = bin(op, std::move(a), std::move(b));
        }
        return a;
    }
    P unary()
    {
        if (isOp("-")) {
            adv();
            P a = unary();
            if (!a)
                return nullptr;
            auto n = mk(NK::Neg);
            n->kids.push_back(std::move(a));
            return n;
        }
        if (isOp("+")) {
            adv();
            return unary();
        }
        if (isOp("!") || (cur.t == Tok::Id && cur.s == "not")) {
            adv();
            P a = unary();
            if (!a)
                return nullptr;
            auto n = mk(NK::Not);
            n->kids.push_back(std::move(a));
            return n;
        }
        return power();
    }
    P power()
    {
        P a = primary();
        if (a && isOp("^")) {
            adv();
            P b = unary();  // правоассоциативно: 2^-x, 2^3^2
            if (!b)
                return nullptr;
            return bin(O_POW, std::move(a), std::move(b));
        }
        return a;
    }
    P primary()
    {
        if (cur.t == Tok::Num) {
            auto n = mk(NK::Num);
            n->num = cur.num;
            adv();
            return n;
        }
        if (cur.t == Tok::LP) {
            adv();
            P e = expr();
            if (!e)
                return nullptr;
            if (cur.t != Tok::RP) {
                err = T("не хватает «)»");
                return nullptr;
            }
            adv();
            return e;
        }
        if (cur.t == Tok::Id) {
            QString id = cur.s;
            adv();
            if (cur.t == Tok::LP) {
                const FuncDef *fd = nullptr;
                for (const auto &f : kFuncs)
                    if (id == QLatin1String(f.name))
                        fd = &f;
                if (!fd) {
                    err = T("неизвестная функция «%1»").arg(id);
                    return nullptr;
                }
                adv();
                auto n = mk(NK::Func);
                n->fn = fd->id;
                if (cur.t != Tok::RP) {
                    while (true) {
                        P a = expr();
                        if (!a)
                            return nullptr;
                        n->kids.push_back(std::move(a));
                        if (cur.t == Tok::Comma) {
                            adv();
                            continue;
                        }
                        break;
                    }
                }
                if (cur.t != Tok::RP) {
                    err = T("не хватает «)» после аргументов %1").arg(id);
                    return nullptr;
                }
                adv();
                if (fd->arity >= 0 && int(n->kids.size()) != fd->arity) {
                    err = T("%1: нужно аргументов — %2").arg(id).arg(fd->arity);
                    return nullptr;
                }
                if (fd->arity < 0 && n->kids.empty()) {
                    err = T("%1: нет аргументов").arg(id);
                    return nullptr;
                }
                return n;
            }
            return ident(id);
        }
        err = cur.t == Tok::End ? T("неожиданный конец выражения")
                                : T("неожиданный символ «%1»").arg(cur.s);
        return nullptr;
    }
    P ident(const QString &id)
    {
        static const QHash<QString, int> slotMap = {
            {"x", 0}, {"y", 1}, {"y0", 1}, {"y1", 2}, {"z", 2}, {"y2", 3}, {"y3", 4}, {"y4", 5},
            {"i", 10}, {"index", 10}, {"n", 11}, {"$t", 12}, {"t", 12},
        };
        if (id == "pi") {
            auto n = mk(NK::Num);
            n->num = M_PI;
            return n;
        }
        if (id == "e") {
            auto n = mk(NK::Num);
            n->num = M_E;
            return n;
        }
        if (id == "nan") {
            auto n = mk(NK::Num);
            n->num = NaN;
            return n;
        }
        if (id == "inf") {
            auto n = mk(NK::Num);
            n->num = std::numeric_limits<double>::infinity();
            return n;
        }
        auto it = slotMap.find(id);
        if (it != slotMap.end()) {
            auto n = mk(NK::Var);
            n->slot = *it;
            if (!vars.contains(id))
                vars << id;
            return n;
        }
        static const QRegularExpression par("^a(\\d)$");
        auto m = par.match(id);
        if (m.hasMatch()) {
            auto n = mk(NK::Param);
            n->slot = m.captured(1).toInt();
            if (!vars.contains(id))
                vars << id;
            return n;
        }
        static const QRegularExpression refre("^(g\\d+\\.)?s\\d+\\.(x|y|y1|y2|y3|y4|z)$");
        if (refre.match(id).hasMatch()) {
            auto n = mk(NK::Ref);
            n->ref = id;
            if (!vars.contains(id))
                vars << id;
            return n;
        }
        err = T("неизвестная переменная «%1»").arg(id);
        return nullptr;
    }
};

double callFn(int id, const double *a, int n)
{
    switch (id) {
    case F_SIN: return std::sin(a[0]);
    case F_COS: return std::cos(a[0]);
    case F_TAN: return std::tan(a[0]);
    case F_ASIN: return std::asin(a[0]);
    case F_ACOS: return std::acos(a[0]);
    case F_ATAN: return std::atan(a[0]);
    case F_ATAN2: return std::atan2(a[0], a[1]);
    case F_SINH: return std::sinh(a[0]);
    case F_COSH: return std::cosh(a[0]);
    case F_TANH: return std::tanh(a[0]);
    case F_ASINH: return std::asinh(a[0]);
    case F_ACOSH: return std::acosh(a[0]);
    case F_ATANH: return std::atanh(a[0]);
    case F_EXP: return std::exp(a[0]);
    case F_LN: return std::log(a[0]);
    case F_LOG10: return std::log10(a[0]);
    case F_LOG2: return std::log2(a[0]);
    case F_SQRT: return std::sqrt(a[0]);
    case F_CBRT: return std::cbrt(a[0]);
    case F_ABS: return std::fabs(a[0]);
    case F_SIGN: return a[0] > 0 ? 1.0 : a[0] < 0 ? -1.0 : 0.0;
    case F_FLOOR: return std::floor(a[0]);
    case F_CEIL: return std::ceil(a[0]);
    case F_ROUND: return std::round(a[0]);
    case F_TRUNC: return std::trunc(a[0]);
    case F_MIN: {
        double r = a[0];
        for (int k = 1; k < n; ++k) r = std::fmin(r, a[k]);
        return r;
    }
    case F_MAX: {
        double r = a[0];
        for (int k = 1; k < n; ++k) r = std::fmax(r, a[k]);
        return r;
    }
    case F_POW: return std::pow(a[0], a[1]);
    case F_MOD: return std::fmod(a[0], a[1]);
    case F_HYPOT: return std::hypot(a[0], a[1]);
    case F_ERF: return std::erf(a[0]);
    case F_ERFC: return std::erfc(a[0]);
    case F_GAMMA: return std::tgamma(a[0]);
    case F_LGAMMA: return std::lgamma(a[0]);
    case F_J0: return ::j0(a[0]);
    case F_J1: return ::j1(a[0]);
    case F_Y0: return ::y0(a[0]);
    case F_Y1: return ::y1(a[0]);
    case F_RAND: return std::uniform_real_distribution<double>(0, 1)(rng());
    case F_RNORM: return std::normal_distribution<double>(a[0], a[1] > 0 ? a[1] : 1)(rng());
    case F_IRAND: return std::floor(std::uniform_real_distribution<double>(0, a[0])(rng()));
    case F_DEG: return a[0] * 180.0 / M_PI;
    case F_RAD: return a[0] * M_PI / 180.0;
    case F_HEAV: return a[0] >= 0 ? 1.0 : 0.0;
    case F_ISNAN: return std::isnan(a[0]) ? 1.0 : 0.0;
    case F_SQR: return a[0] * a[0];
    case F_FAC: return std::tgamma(a[0] + 1);
    case F_NORMPDF: return std::exp(-0.5 * a[0] * a[0]) / std::sqrt(2 * M_PI);
    }
    return NaN;
}

double evalNode(const ExprNode *nd, const Expr::Ctx &c)
{
    switch (nd->kind) {
    case NK::Num:
        return nd->num;
    case NK::Var:
        if (nd->slot == 10)
            return c.row;
        if (nd->slot == 11)
            return c.n;
        if (nd->slot == 12)
            return c.t;
        if (c.cols && nd->slot < c.cols->size()) {
            const QVector<double> *col = (*c.cols)[nd->slot];
            if (col && c.row < col->size())
                return (*col)[c.row];
        }
        return NaN;
    case NK::Param:
        return c.params ? c.params[nd->slot] : 0.0;
    case NK::Ref: {
        if (!c.resolver)
            return NaN;
        const QVector<double> *col = c.resolver(nd->ref);
        if (col && c.row < col->size())
            return (*col)[c.row];
        return NaN;
    }
    case NK::Func: {
        double a[16];
        int n = qMin(int(nd->kids.size()), 16);
        for (int k = 0; k < n; ++k)
            a[k] = evalNode(nd->kids[k].get(), c);
        return callFn(nd->fn, a, n);
    }
    case NK::Neg:
        return -evalNode(nd->kids[0].get(), c);
    case NK::Not:
        return evalNode(nd->kids[0].get(), c) == 0 ? 1.0 : 0.0;
    case NK::Tern:
        return evalNode(nd->kids[0].get(), c) != 0 ? evalNode(nd->kids[1].get(), c)
                                                   : evalNode(nd->kids[2].get(), c);
    case NK::Bin: {
        const double a = evalNode(nd->kids[0].get(), c);
        if (nd->op == O_AND && a == 0)
            return 0.0;
        if (nd->op == O_OR && a != 0)
            return 1.0;
        const double b = evalNode(nd->kids[1].get(), c);
        switch (nd->op) {
        case O_ADD: return a + b;
        case O_SUB: return a - b;
        case O_MUL: return a * b;
        case O_DIV: return a / b;
        case O_POW: return std::pow(a, b);
        case O_MODOP: return std::fmod(a, b);
        case O_LT: return a < b;
        case O_GT: return a > b;
        case O_LE: return a <= b;
        case O_GE: return a >= b;
        case O_EQ: return a == b;
        case O_NE: return a != b;
        case O_AND: return b != 0;
        case O_OR: return b != 0;
        }
    }
    }
    return NaN;
}

}  // namespace

Expr::Expr() = default;
Expr::~Expr() = default;

bool Expr::parse(const QString &text, QString *error)
{
    m_root.reset();
    m_vars.clear();
    ParserX p(text, m_vars);
    auto r = p.expr();
    if (r && p.cur.t != Tok::End) {
        p.err = T("лишнее в конце выражения: «%1»").arg(text.mid(qMax(0, p.lx.pos - 1)));
        r.reset();
    }
    if (!r) {
        if (error)
            *error = p.err.isEmpty() ? QStringLiteral("ошибка в выражении") : p.err;
        return false;
    }
    m_root = std::move(r);
    return true;
}

double Expr::eval(const Ctx &c) const
{
    return m_root ? evalNode(m_root.get(), c) : NaN;
}

QStringList Expr::variables() const { return m_vars; }

bool Expr::usesParam(int k) const { return m_vars.contains(QString("a%1").arg(k)); }

bool Expr::splitAssignments(const QString &text, QVector<Assignment> &out, QString *error)
{
    out.clear();
    static const QRegularExpression sep("[;\\n]");
    static const QRegularExpression asg("^\\s*([A-Za-z][A-Za-z0-9]*)\\s*=(?!=)\\s*(.+)$");
    for (const QString &part : text.split(sep, Qt::SkipEmptyParts)) {
        if (part.trimmed().isEmpty())
            continue;
        auto m = asg.match(part);
        if (!m.hasMatch()) {
            if (error)
                *error = QStringLiteral("ожидалось присваивание вида «y = …»: %1").arg(part.trimmed());
            return false;
        }
        QString t = m.captured(1).toLower();
        if (t == "z")
            t = "y1";
        static const QStringList targets = {"x", "y", "y1", "y2", "y3", "y4"};
        if (!targets.contains(t)) {
            if (error)
                *error = QStringLiteral("присваивать можно только x, y, y1…y4: %1").arg(t);
            return false;
        }
        out.append({t, m.captured(2).trimmed()});
    }
    if (out.isEmpty()) {
        if (error)
            *error = QStringLiteral("пустая формула");
        return false;
    }
    return true;
}

bool evalOnArray(const QString &expr, const QVector<double> &x, QVector<double> &out,
                 QString *error, const double *params)
{
    Expr e;
    if (!e.parse(expr, error))
        return false;
    QVector<const QVector<double> *> cols{&x};
    Expr::Ctx c;
    c.cols = &cols;
    c.n = x.size();
    c.params = params;
    out.resize(x.size());
    for (int i = 0; i < x.size(); ++i) {
        c.row = i;
        out[i] = e.eval(c);
    }
    return true;
}

}  // namespace ng
