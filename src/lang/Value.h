#pragma once
#include <string>
#include <vector>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Value — the runtime type produced by evaluating an expression.
//
// OpenSCAD types: number (double), bool, string, vector (list), range, undef.
// Strings are included for completeness but not yet used in geometry.
// ---------------------------------------------------------------------------
struct Value {
    enum class Tag { Number, Bool, Vector, String, Range, Undef };

    Tag tag = Tag::Undef;

    double             number  = 0.0;
    bool               boolean = false;
    std::vector<Value> vec;
    std::string        str;
    // Range only — kept lazy (not expanded into `vec`) so a range's identity
    // (echo() prints "[a:b:c]", not an expanded list) and cost stay distinct
    // from an actual list; consumers that need the iterated values call
    // Interpreter::expandRange(rangeStart, rangeStep, rangeEnd).
    double rangeStart = 0.0;
    double rangeStep  = 1.0;
    double rangeEnd   = 0.0;

    // ---- Factories --------------------------------------------------------
    static Value fromNumber(double v)  { Value r; r.tag = Tag::Number; r.number  = v;             return r; }
    static Value fromBool  (bool   v)  { Value r; r.tag = Tag::Bool;   r.boolean = v;             return r; }
    static Value fromVec   (std::vector<Value> v) { Value r; r.tag = Tag::Vector; r.vec = std::move(v); return r; }
    static Value fromString(std::string s)        { Value r; r.tag = Tag::String; r.str = std::move(s); return r; }
    static Value fromRange(double start, double step, double end) {
        Value r; r.tag = Tag::Range;
        r.rangeStart = start; r.rangeStep = step; r.rangeEnd = end;
        return r;
    }
    static Value undef()               { return {}; }

    // ---- Type queries -----------------------------------------------------
    bool isNumber() const { return tag == Tag::Number; }
    bool isBool()   const { return tag == Tag::Bool;   }
    bool isVector() const { return tag == Tag::Vector;  }
    bool isString() const { return tag == Tag::String;  }
    bool isRange()  const { return tag == Tag::Range;   }
    bool isUndef()  const { return tag == Tag::Undef;   }

    // ---- Accessors --------------------------------------------------------
    double             asNumber() const { return number;  }
    bool               asBool()   const { return boolean; }
    const std::vector<Value>& asVec() const { return vec; }
    const std::string& asString() const { return str;     }

    // ---- Truthiness (OpenSCAD semantics) ----------------------------------
    // 0, empty vector, and undef are falsy; everything else truthy.
    explicit operator bool() const {
        switch (tag) {
        case Tag::Number: return number != 0.0;
        case Tag::Bool:   return boolean;
        case Tag::Vector: return !vec.empty();
        case Tag::String: return !str.empty();
        case Tag::Range:  return true;
        default:          return false;
        }
    }
};

} // namespace chisel::lang
