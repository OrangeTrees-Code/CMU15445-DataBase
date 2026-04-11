SELECT
    c.name AS COACH_NAME,
    COUNT(*) as MEDAL_NUMBER
FROM
    (
        SELECT
            a.country_code AS country_code,
            m.discipline AS disciplines
        FROM
            medals AS m
            JOIN athletes as a ON m.winner_code = a.code
        UNION ALL
        SELECT
            t.country_code AS country_code,
            m.discipline AS disciplines
        FROM
            medals AS m
            JOIN teams AS t ON m.winner_code = t.code
    ) AS c_d
    JOIN coaches AS c ON c.country_code = c_d.country_code
    AND c.discipline = c_d.disciplines
GROUP BY
    c.name
ORDER BY
    MEDAL_NUMBER DESC,
    COACH_NAME ASC;