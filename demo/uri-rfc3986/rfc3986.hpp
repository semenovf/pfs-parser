////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2021 Vladislav Trifochkin
//
// This file is part of [pfs-parser](https://github.com/semenovf/pfs-parser) library.
//
// Changelog:
//      2021.02.25 Initial version
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "pfs/parser/abnf/parser.hpp"
#include "pfs/parser/line_counter_iterator.hpp"

namespace grammar {
namespace rfc3986 {

using forward_iterator = pfs::parser::line_counter_iterator<std::string::const_iterator>;

class context
{
    int rulenames = 0;
    std::string _lastError;

public:
    std::string lastError () const
    {
        return _lastError;
    }

    // ProseContext
    void prose (forward_iterator, forward_iterator) {}

    // NumberContext
    void first_number (pfs::parser::abnf::number_flag, forward_iterator, forward_iterator) {}
    void last_number (pfs::parser::abnf::number_flag, forward_iterator, forward_iterator) {}
    void next_number (pfs::parser::abnf::number_flag, forward_iterator, forward_iterator) {}

    // QuotedStringContext
    void quoted_string (forward_iterator, forward_iterator) {}

    void error (std::error_code ec)
    {
        _lastError = ec.message();
    }

    size_t max_quoted_string_length () { return 0; }

    // RepeatContext
    void repeat (forward_iterator first_from, forward_iterator last_from
        , forward_iterator first_to, forward_iterator last_to) {}

    // CommentContext
    void comment (forward_iterator first, forward_iterator last) {}

    // RulenameContext
    void rulename (forward_iterator first, forward_iterator last) {}

    // RepetitionContext
    void begin_repetition () {}
    void end_repetition (bool success) {}

    // ConcatenationContext
    void begin_concatenation () {}
    void end_concatenation (bool success) {}

    // DefinedAsContext
    void accept_basic_rule_definition () { rulenames++; }
    void accept_incremental_alternatives () {}
};

}} // grammar::rfc3986