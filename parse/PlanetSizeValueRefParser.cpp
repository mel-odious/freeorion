#include "ValueRefParserImpl.h"

#include "EnumParser.h"


namespace {
    struct planet_size_parser_rules {
        planet_size_parser_rules() {
            qi::_1_type _1;
            qi::_val_type _val;
            using phoenix::new_;
            using phoenix::push_back;

            const parse::lexer& tok = parse::lexer::instance();

            variable_name
                %=   tok.PlanetSize_
                |    tok.NextLargerPlanetSize_
                |    tok.NextSmallerPlanetSize_
                ;

            constant
                =    parse::planet_size_enum() [ _val = new_<ValueRef::Constant<PlanetSize> >(_1) ]
                ;

            initialize_bound_variable_parser<PlanetSize>(bound_variable, variable_name);

            statistic_sub_value_ref
                =   constant
                |   bound_variable
                ;

            initialize_nonnumeric_expression_parsers<PlanetSize>(function_expr, operated_expr, expr, primary_expr);

            initialize_nonnumeric_statistic_parser<PlanetSize>(statistic, statistic_sub_value_ref);

            primary_expr
                =   constant
                |   bound_variable
                |   statistic
                ;

            variable_name.name("PlanetSize variable name (e.g., PlanetSize)");
            constant.name("PlanetSize");
            bound_variable.name("PlanetSize variable");
            statistic.name("PlanetSize statistic");
            primary_expr.name("PlanetSize expression");

#if DEBUG_VALUEREF_PARSERS
            debug(variable_name);
            debug(constant);
            debug(bound_variable);
            debug(statistic);
            debug(primary_expr);
#endif
        }

        typedef parse::value_ref_rule<PlanetSize> rule;

        name_token_rule variable_name;
        rule            constant;
        variable_rule<PlanetSize> bound_variable;
        rule            statistic_sub_value_ref;
        statistic_rule<PlanetSize> statistic;
        expression_rule<PlanetSize> function_expr;
        expression_rule<PlanetSize> operated_expr;
        rule            expr;
        rule            primary_expr;
    };
}


namespace parse {
    value_ref_rule<PlanetSize>& planet_size_value_ref()
    {
        static planet_size_parser_rules retval;
        return retval.expr;
    }
}
