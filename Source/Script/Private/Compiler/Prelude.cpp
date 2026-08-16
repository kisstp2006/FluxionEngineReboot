// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#include <Compiler/Prelude.hpp>

#include <string>

namespace Fluxion::Script
{

const char* PreludeSourceName()
{
    return "<prelude>";
}

namespace
{

// The value types every program can count on. All of them are written in
// the language itself rather than built into the compiler: each is a
// struct whose fields are numbers, so what a program gets is exactly what
// it would have got by declaring them, and nothing here needs a rule of
// its own anywhere in the compiler or the machine.
//
// There are no operators to overload, so the arithmetic is spelled out as
// methods -- `a.Add(b)`, `a.Scale(2.0f)` -- and each answers with a fresh
// value rather than changing the one it ran on, which is what a copy on
// every assignment already implies.
const char* MathTypesSource()
{
    return
        "struct Vector2\n"
        "{\n"
        "    float x;\n"
        "    float y;\n"
        "\n"
        "    Vector2(float px, float py) { this.x = px; this.y = py; }\n"
        "\n"
        "    Vector2 Add(Vector2 other) { return new Vector2(this.x + other.x, this.y + other.y); }\n"
        "    Vector2 Subtract(Vector2 other) { return new Vector2(this.x - other.x, this.y - other.y); }\n"
        "    Vector2 Scale(float factor) { return new Vector2(this.x * factor, this.y * factor); }\n"
        "    Vector2 Negate() { return new Vector2(0.0f - this.x, 0.0f - this.y); }\n"
        "\n"
        "    float Dot(Vector2 other) { return this.x * other.x + this.y * other.y; }\n"
        "    float LengthSquared() { return this.x * this.x + this.y * this.y; }\n"
        "    float Length() { return Math.Sqrt(this.LengthSquared()); }\n"
        "    float Distance(Vector2 other) { return this.Subtract(other).Length(); }\n"
        "\n"
        "    Vector2 Normalized()\n"
        "    {\n"
        "        float length = this.Length();\n"
        "        if (length == 0.0f) { return new Vector2(0.0f, 0.0f); }\n"
        "        return this.Scale(1.0f / length);\n"
        "    }\n"
        "\n"
        "    Vector2 Lerp(Vector2 target, float t) { return this.Add(target.Subtract(this).Scale(t)); }\n"
        "}\n"
        "\n"
        "struct Vector3\n"
        "{\n"
        "    float x;\n"
        "    float y;\n"
        "    float z;\n"
        "\n"
        "    Vector3(float px, float py, float pz) { this.x = px; this.y = py; this.z = pz; }\n"
        "\n"
        "    Vector3 Add(Vector3 other) { return new Vector3(this.x + other.x, this.y + other.y, this.z + other.z); }\n"
        "    Vector3 Subtract(Vector3 other) { return new Vector3(this.x - other.x, this.y - other.y, this.z - other.z); }\n"
        "    Vector3 Scale(float factor) { return new Vector3(this.x * factor, this.y * factor, this.z * factor); }\n"
        "    Vector3 Negate() { return new Vector3(0.0f - this.x, 0.0f - this.y, 0.0f - this.z); }\n"
        "\n"
        "    float Dot(Vector3 other) { return this.x * other.x + this.y * other.y + this.z * other.z; }\n"
        "\n"
        "    Vector3 Cross(Vector3 other)\n"
        "    {\n"
        "        return new Vector3(\n"
        "            this.y * other.z - this.z * other.y,\n"
        "            this.z * other.x - this.x * other.z,\n"
        "            this.x * other.y - this.y * other.x);\n"
        "    }\n"
        "\n"
        "    float LengthSquared() { return this.x * this.x + this.y * this.y + this.z * this.z; }\n"
        "    float Length() { return Math.Sqrt(this.LengthSquared()); }\n"
        "    float Distance(Vector3 other) { return this.Subtract(other).Length(); }\n"
        "\n"
        "    Vector3 Normalized()\n"
        "    {\n"
        "        float length = this.Length();\n"
        "        if (length == 0.0f) { return new Vector3(0.0f, 0.0f, 0.0f); }\n"
        "        return this.Scale(1.0f / length);\n"
        "    }\n"
        "\n"
        "    Vector3 Lerp(Vector3 target, float t) { return this.Add(target.Subtract(this).Scale(t)); }\n"
        "}\n"
        "\n"
        "struct Vector4\n"
        "{\n"
        "    float x;\n"
        "    float y;\n"
        "    float z;\n"
        "    float w;\n"
        "\n"
        "    Vector4(float px, float py, float pz, float pw) { this.x = px; this.y = py; this.z = pz; this.w = pw; }\n"
        "\n"
        "    Vector4 Add(Vector4 other) { return new Vector4(this.x + other.x, this.y + other.y, this.z + other.z, this.w + other.w); }\n"
        "    Vector4 Subtract(Vector4 other) { return new Vector4(this.x - other.x, this.y - other.y, this.z - other.z, this.w - other.w); }\n"
        "    Vector4 Scale(float factor) { return new Vector4(this.x * factor, this.y * factor, this.z * factor, this.w * factor); }\n"
        "\n"
        "    float Dot(Vector4 other) { return this.x * other.x + this.y * other.y + this.z * other.z + this.w * other.w; }\n"
        "    float LengthSquared() { return this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w; }\n"
        "    float Length() { return Math.Sqrt(this.LengthSquared()); }\n"
        "\n"
        "    Vector3 AsVector3() { return new Vector3(this.x, this.y, this.z); }\n"
        "\n"
        "    Vector4 Normalized()\n"
        "    {\n"
        "        float length = this.Length();\n"
        "        if (length == 0.0f) { return new Vector4(0.0f, 0.0f, 0.0f, 0.0f); }\n"
        "        return this.Scale(1.0f / length);\n"
        "    }\n"
        "}\n"
        "\n"
        // A rotation, held as the four numbers that describe it. Every
        // method here assumes a rotation of unit length, which is what
        // Identity and FromAxisAngle both produce and what Multiply
        // preserves; Normalized is there for the drift that repeated
        // multiplication eventually causes.
        "struct Quaternion\n"
        "{\n"
        "    float x;\n"
        "    float y;\n"
        "    float z;\n"
        "    float w;\n"
        "\n"
        "    Quaternion(float px, float py, float pz, float pw) { this.x = px; this.y = py; this.z = pz; this.w = pw; }\n"
        "\n"
        "    static Quaternion Identity() { return new Quaternion(0.0f, 0.0f, 0.0f, 1.0f); }\n"
        "\n"
        "    static Quaternion FromAxisAngle(Vector3 axis, float radians)\n"
        "    {\n"
        "        Vector3 unit = axis.Normalized();\n"
        "        float half = radians * 0.5f;\n"
        "        float sine = Math.Sin(half);\n"
        "        return new Quaternion(unit.x * sine, unit.y * sine, unit.z * sine, Math.Cos(half));\n"
        "    }\n"
        "\n"
        "    Quaternion Multiply(Quaternion other)\n"
        "    {\n"
        "        return new Quaternion(\n"
        "            this.w * other.x + this.x * other.w + this.y * other.z - this.z * other.y,\n"
        "            this.w * other.y - this.x * other.z + this.y * other.w + this.z * other.x,\n"
        "            this.w * other.z + this.x * other.y - this.y * other.x + this.z * other.w,\n"
        "            this.w * other.w - this.x * other.x - this.y * other.y - this.z * other.z);\n"
        "    }\n"
        "\n"
        "    Quaternion Conjugate() { return new Quaternion(0.0f - this.x, 0.0f - this.y, 0.0f - this.z, this.w); }\n"
        "\n"
        "    float LengthSquared() { return this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w; }\n"
        "    float Length() { return Math.Sqrt(this.LengthSquared()); }\n"
        "\n"
        "    Quaternion Normalized()\n"
        "    {\n"
        "        float length = this.Length();\n"
        "        if (length == 0.0f) { return Quaternion.Identity(); }\n"
        "        float inverse = 1.0f / length;\n"
        "        return new Quaternion(this.x * inverse, this.y * inverse, this.z * inverse, this.w * inverse);\n"
        "    }\n"
        "\n"
        "    Vector3 Axis() { return new Vector3(this.x, this.y, this.z); }\n"
        "\n"
        // v + 2w(q x v) + 2(q x (q x v)), written with the cross products
        // shared, which is the same rotation as q*v*conjugate(q) without
        // building either of the two products in between.
        "    Vector3 Rotate(Vector3 point)\n"
        "    {\n"
        "        Vector3 axis = this.Axis();\n"
        "        Vector3 turned = axis.Cross(point).Scale(2.0f);\n"
        "        return point.Add(turned.Scale(this.w)).Add(axis.Cross(turned));\n"
        "    }\n"
        "}\n"
        "\n"
        "struct Color\n"
        "{\n"
        "    float r;\n"
        "    float g;\n"
        "    float b;\n"
        "    float a;\n"
        "\n"
        "    Color(float red, float green, float blue, float alpha) { this.r = red; this.g = green; this.b = blue; this.a = alpha; }\n"
        "\n"
        "    static Color Black() { return new Color(0.0f, 0.0f, 0.0f, 1.0f); }\n"
        "    static Color White() { return new Color(1.0f, 1.0f, 1.0f, 1.0f); }\n"
        "    static Color Transparent() { return new Color(0.0f, 0.0f, 0.0f, 0.0f); }\n"
        "\n"
        "    static Color FromBytes(int red, int green, int blue, int alpha)\n"
        "    {\n"
        "        return new Color(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);\n"
        "    }\n"
        "\n"
        "    Color WithAlpha(float alpha) { return new Color(this.r, this.g, this.b, alpha); }\n"
        "    Color Add(Color other) { return new Color(this.r + other.r, this.g + other.g, this.b + other.b, this.a + other.a); }\n"
        "\n"
        // Brightness only: an alpha scaled along with the colour would
        // make every fade also a fade to transparent, which is almost
        // never what was meant.
        "    Color Scale(float factor) { return new Color(this.r * factor, this.g * factor, this.b * factor, this.a); }\n"
        "\n"
        "    Color Lerp(Color target, float t)\n"
        "    {\n"
        "        return new Color(\n"
        "            this.r + (target.r - this.r) * t,\n"
        "            this.g + (target.g - this.g) * t,\n"
        "            this.b + (target.b - this.b) * t,\n"
        "            this.a + (target.a - this.a) * t);\n"
        "    }\n"
        "\n"
        "    Vector4 AsVector4() { return new Vector4(this.r, this.g, this.b, this.a); }\n"
        "}\n";
}

// The arithmetic every program reaches for. All of it is written here
// rather than built in, for the same reason the value types above are: a
// minimum is a comparison, a clamp is two of them and an interpolation is
// a multiply and an add, and none of that is something the machine needs
// a rule for. What is left -- a square root, a sine, a floor -- is what
// arithmetic genuinely cannot express, and only those cross over to the
// machine.
//
// Everything works in floats. A whole number widens into one on its own,
// so `Mathf.Max(hits, 1)` is written the same way as it would be with
// floats and answers with a float.
const char* MathfSource()
{
    return
        "static class Mathf\n"
        "{\n"
        // The language has no way to declare a constant, so the ratio is
        // a method like everything else here.
        "    static float PI() { return 3.14159265f; }\n"
        "\n"
        "    static float Abs(float value)\n"
        "    {\n"
        "        if (value < 0.0f) { return 0.0f - value; }\n"
        "        return value;\n"
        "    }\n"
        "\n"
        "    static float Min(float a, float b)\n"
        "    {\n"
        "        if (a < b) { return a; }\n"
        "        return b;\n"
        "    }\n"
        "\n"
        "    static float Max(float a, float b)\n"
        "    {\n"
        "        if (a > b) { return a; }\n"
        "        return b;\n"
        "    }\n"
        "\n"
        // A low bound above the high one wins, because the low one is
        // read first: with two answers both defensible, the one that is
        // the same every time is worth more than either.
        "    static float Clamp(float value, float low, float high)\n"
        "    {\n"
        "        if (value < low) { return low; }\n"
        "        if (value > high) { return high; }\n"
        "        return value;\n"
        "    }\n"
        "\n"
        "    static float Clamp01(float value) { return Clamp(value, 0.0f, 1.0f); }\n"
        "\n"
        // The usual one holds inside the two ends it was given, which is
        // what almost every use wants; the other is there for the uses
        // that genuinely mean to carry on past them.
        "    static float Lerp(float from, float to, float t) { return from + (to - from) * Clamp01(t); }\n"
        "    static float LerpUnclamped(float from, float to, float t) { return from + (to - from) * t; }\n"
        "\n"
        // How far along `value` sits between the two ends -- the question
        // Lerp answers backwards. Two ends that are the same place have
        // no answer, and zero is the one that keeps whatever is built on
        // it from producing a number no comparison behaves sensibly
        // around.
        "    static float InverseLerp(float from, float to, float value)\n"
        "    {\n"
        "        if (from == to) { return 0.0f; }\n"
        "        return Clamp01((value - from) / (to - from));\n"
        "    }\n"
        "\n"
        "    static float Sqrt(float value) { return Math.Sqrt(value); }\n"
        "    static float Sin(float radians) { return Math.Sin(radians); }\n"
        "    static float Cos(float radians) { return Math.Cos(radians); }\n"
        "    static float Floor(float value) { return Math.Floor(value); }\n"
        "    static float Ceil(float value) { return Math.Ceil(value); }\n"
        "\n"
        // Exactly halfway goes upwards. Adding a half and taking the
        // floor says the same thing in one line, but not for every
        // number: for the largest float below a half, the sum is not
        // representable and lands on one, so a value below a half would
        // round up. Measuring the distance from the floor instead is
        // exact for every value, because a fractional part never needs
        // more precision than the number it came from.
        "    static float Round(float value)\n"
        "    {\n"
        "        float lower = Math.Floor(value);\n"
        "        if (value - lower >= 0.5f) { return lower + 1.0f; }\n"
        "        return lower;\n"
        "    }\n"
        "}\n";
}

// A source of numbers that is asked for a seed and then produces the very
// same sequence from it every time, on every machine. That is the whole
// point of it: a run that went wrong is a run that can be had again, and
// a test can say what the third number will be.
//
// The step is a multiply and a remainder over a prime, arranged so that
// neither of the two products it forms can ever leave the range a whole
// number holds -- which is why it is split into a high and a low part
// rather than written as the single multiplication it stands for.
const char* RandomSource()
{
    return
        "class Random\n"
        "{\n"
        "    int state;\n"
        "\n"
        "    Random(int seed) { this.SetSeed(seed); }\n"
        "\n"
        // Every seed is welcome; what it becomes is a number the step is
        // defined over. Zero is the one number the step cannot move, so
        // it is the one number moved aside.
        "    void SetSeed(int seed)\n"
        "    {\n"
        "        int chosen = seed % 2147483647;\n"
        "        if (chosen < 0) { chosen = chosen + 2147483647; }\n"
        "        if (chosen == 0) { chosen = 1; }\n"
        "        this.state = chosen;\n"
        "    }\n"
        "\n"
        "    int Seed() { return this.state; }\n"
        "\n"
        "    int NextInt()\n"
        "    {\n"
        "        int high = this.state / 127773;\n"
        "        int low = this.state % 127773;\n"
        "        int stepped = 16807 * low - 2836 * high;\n"
        "        if (stepped < 0) { stepped = stepped + 2147483647; }\n"
        "        this.state = stepped;\n"
        "        return stepped;\n"
        "    }\n"
        "\n"
        // Never quite one, and never quite zero: the step only ever
        // produces numbers strictly inside the range it is defined over.
        "    float NextFloat() { return this.NextInt() / 2147483647.0f; }\n"
        "\n"
        "    bool NextBool() { return this.NextInt() % 2 == 0; }\n"
        "\n"
        "    float Range(float low, float high) { return low + (high - low) * this.NextFloat(); }\n"
        "\n"
        // The high end is not one of the answers, so an empty span
        // answers with the only number it contains nothing of. The
        // remainder leaves the earliest few numbers of the span very
        // slightly likelier than the rest, which is worth far less than
        // the sequence being the same everywhere.
        "    int RangeInt(int low, int high)\n"
        "    {\n"
        "        if (high <= low) { return low; }\n"
        "        return low + this.NextInt() % (high - low);\n"
        "    }\n"
        "}\n";
}

const char* ListSource()
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

} // namespace

const char* PreludeSource()
{
    // The pieces are written apart for the sake of reading them and
    // joined once, the first time anyone asks: what the compiler wants is
    // a single source, and building it here keeps that from being a
    // reason to write them as one.
    static const std::string whole = std::string(MathTypesSource()) + MathfSource() + RandomSource() + ListSource();
    return whole.c_str();
}

} // namespace Fluxion::Script
