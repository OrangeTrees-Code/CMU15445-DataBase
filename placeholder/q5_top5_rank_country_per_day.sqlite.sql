WITH
    date_country AS (
        SELECT
            r.date,
            a_t.country_code,
            c.Population,
            c."GDP ($ per capita)"
        FROM
            results AS r
            JOIN (
                SELECT
                    code,
                    country_code
                FROM
                    athletes
                UNION
                SELECT
                    code,
                    country_code
                FROM
                    teams
            ) AS a_t ON a_t.code = r.participant_code
            JOIN countries AS c ON a_t.country_code = c.code
        WHERE
            r.rank <= 5
            AND a_t.country_code IS NOT NULL
            AND c.Population IS NOT NULL
            AND c."GDP ($ per capita)" IS NOT NULL
    ),
    country_counts AS (
        SELECT
            date,
            country_code,
            Population,
            "GDP ($ per capita)",
            COUNT(*) AS cnt
        FROM
            date_country
        GROUP BY
            date,
            country_code,
            Population,
            "GDP ($ per capita)"
    ),
    ranked AS (
        SELECT
            *,
            ROW_NUMBER() OVER (
                PARTITION BY
                    date
                ORDER BY
                    cnt DESC,
                    country_code ASC
            ) AS rn
        FROM
            country_counts
    ),
    final AS (
        SELECT
            date,
            country_code,
            cnt,
            Population,
            "GDP ($ per capita)"
        FROM
            ranked
        WHERE
            rn = 1
    )
SELECT
    f.date AS DATE,
    f.country_code AS COUNTRY_CODE,
    f.cnt AS TOP5_APPEARANCES,
    g.gdp_rank AS GDP_RANK,
    p.pop_rank AS POPULATION_RANK
FROM
    final f
    JOIN (
        SELECT
            code AS country_code,
            RANK() OVER (
                ORDER BY
                    "GDP ($ per capita)" DESC
            ) AS gdp_rank
        FROM
            countries
    ) g ON f.country_code = g.country_code
    JOIN (
        SELECT
            code AS country_code,
            RANK() OVER (
                ORDER BY
                    Population DESC
            ) AS pop_rank
        FROM
            countries
    ) p ON f.country_code = p.country_code
ORDER BY
    f.date ASC;