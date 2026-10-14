// Вычисление выражений в стиле Grace: y = a0*exp(-x/a1) + sin(2*pi*x) ...
//
// Переменные: x, y, y1..y4 (z — синоним y1), i (номер строки), n (число
// строк), a0..a9 (параметры аппроксимации), $t (для формул подписей),
// sN.x / sN.y / gM.sN.y — столбцы других наборов.
// Операторы: + - * / ^ ** % < > <= >= == != && || ! ?: ; функции — см. expr.cpp.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

namespace ng {

struct ExprNode;

class Expr {
public:
    Expr();
    ~Expr();
    Expr(const Expr &) = delete;
    Expr &operator=(const Expr &) = delete;

    bool parse(const QString &text, QString *error = nullptr);
    bool ok() const { return bool(m_root); }

    // Контекст вычисления
    struct Ctx {
        const QVector<const QVector<double> *> *cols = nullptr;  // x y y1 y2 y3 y4
        int row = 0;
        int n = 0;
        const double *params = nullptr;  // a0..a9
        double t = 0;
        // ссылки на другие наборы: имя "s1.y" или "g0.s1.x" → столбец
        std::function<const QVector<double> *(const QString &)> resolver;
    };
    double eval(const Ctx &c) const;
    QStringList variables() const;  // какие переменные встречаются (в нижнем регистре)
    bool usesParam(int k) const;

    // Для нескольких присваиваний "x = ...; y = ..." (разбивает текст)
    struct Assignment {
        QString target;  // x y y1..y4
        QString expr;
    };
    static bool splitAssignments(const QString &text, QVector<Assignment> &out, QString *error);

private:
    std::unique_ptr<ExprNode> m_root;
    QStringList m_vars;
};

// Вычислить выражение от одной переменной x на массиве
bool evalOnArray(const QString &expr, const QVector<double> &x, QVector<double> &out,
                 QString *error = nullptr, const double *params = nullptr);

}  // namespace ng
