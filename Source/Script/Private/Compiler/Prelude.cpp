#include <Compiler/Prelude.hpp>

namespace Fluxion::Script
{

const char* PreludeSourceName()
{
    return "<prelude>";
}

const char* PreludeSource()
{
    // A list holds a sequence and a count of how much of it is in use.
    // Adding past the end replaces the sequence with a longer one and
    // copies what was there, which is the only way its contents ever
    // move.
    //
    // A position outside what has been added is refused the same way a
    // sequence refuses one: by reaching into a sequence that has no
    // elements at all, so the fault a reader sees is the same fault in
    // both cases and there is no second way for one to be reported.
    return
        "class List<T>\n"
        "{\n"
        "    T[] items;\n"
        "    int count;\n"
        "\n"
        "    List()\n"
        "    {\n"
        "        this.items = new T[4];\n"
        "        this.count = 0;\n"
        "    }\n"
        "\n"
        "    int Count() { return this.count; }\n"
        "\n"
        "    void Add(T item)\n"
        "    {\n"
        "        if (this.count == this.items.Length) { this.Grow(); }\n"
        "        this.items[this.count] = item;\n"
        "        this.count = this.count + 1;\n"
        "    }\n"
        "\n"
        "    T Get(int index)\n"
        "    {\n"
        "        if (index < 0 || index >= this.count) { return this.Refuse(); }\n"
        "        return this.items[index];\n"
        "    }\n"
        "\n"
        "    void Set(int index, T value)\n"
        "    {\n"
        "        if (index < 0 || index >= this.count) { this.Refuse(); }\n"
        "        this.items[index] = value;\n"
        "    }\n"
        "\n"
        "    void RemoveAt(int index)\n"
        "    {\n"
        "        if (index < 0 || index >= this.count) { this.Refuse(); }\n"
        "        for (int i = index; i < this.count - 1; i += 1)\n"
        "        {\n"
        "            this.items[i] = this.items[i + 1];\n"
        "        }\n"
        "        T[] blank = new T[1];\n"
        "        this.items[this.count - 1] = blank[0];\n"
        "        this.count = this.count - 1;\n"
        "    }\n"
        "\n"
        "    void Clear()\n"
        "    {\n"
        "        this.items = new T[4];\n"
        "        this.count = 0;\n"
        "    }\n"
        "\n"
        "    void Grow()\n"
        "    {\n"
        "        int wanted = this.items.Length * 2;\n"
        "        if (wanted < 4) { wanted = 4; }\n"
        "        T[] grown = new T[wanted];\n"
        "        for (int i = 0; i < this.count; i += 1)\n"
        "        {\n"
        "            grown[i] = this.items[i];\n"
        "        }\n"
        "        this.items = grown;\n"
        "    }\n"
        "\n"
        "    T Refuse()\n"
        "    {\n"
        "        T[] none = new T[0];\n"
        "        return none[0];\n"
        "    }\n"
        "}\n";
}

} // namespace Fluxion::Script
