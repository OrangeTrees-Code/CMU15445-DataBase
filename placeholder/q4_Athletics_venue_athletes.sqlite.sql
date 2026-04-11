SELECT DISTINCT
    a.name AS ATHLETE_NAME,
    c_p.code AS REPRESENTED_COUNTRY_CODE,
    c_o.code AS NATIONALITY_COUNTRY_CODE
FROM
    (
        SELECT
            r.participant_code AS a_code
        FROM
            venues AS v
            JOIN results AS r ON v.venue = r.venue
        WHERE
            v.disciplines LIKE '%Athletics%'
            AND r.participant_type = 'Person'
        UNION
        SELECT
            t.athletes_code AS a_code
        FROM
            venues AS v
            JOIN results AS r ON v.venue = r.venue
            JOIN teams AS t ON t.code = r.participant_code
        WHERE
            v.disciplines LIKE '%Athletics%'
            AND r.participant_type = 'Team'
    ) AS p
    JOIN athletes AS a ON a.code = p.a_code
    JOIN countries AS c_p ON c_p.code = a.country_code
    JOIN countries AS c_o ON c_o.code = a.nationality_code
WHERE
    c_p.Latitude IS NOT NULL
    AND c_p.Longitude IS NOT NULL
    AND c_O.Latitude IS NOT NULL
    AND c_o.Longitude IS NOT NULL
ORDER BY
    (
        SQRT(
            (c_p.Latitude - c_o.Latitude) * (c_p.Latitude - c_o.Latitude) + (c_p.Longitude - c_o.Longitude) * (c_p.Longitude - c_o.Longitude)
        )
    ) DESC,
    ATHLETE_NAME ASC;

-----------------------------------------------------------------------------------------------------------------------------------------------------
WITH
    athlete_codes AS (
        SELECT
            r.participant_code AS a_code
        FROM
            venues v
            JOIN results r ON v.venue = r.venue
        WHERE
            v.disciplines LIKE '%Athletics%'
            AND r.participant_type = 'Person'
        UNION
        SELECT
            t.athletes_code AS a_code
        FROM
            venues v
            JOIN results r ON v.venue = r.venue
            JOIN teams t ON t.code = r.participant_code
        WHERE
            v.disciplines LIKE '%Athletics%'
            AND r.participant_type = 'Team'
    ),
    valid_athletes AS (
        SELECT
            a.name,
            c_p.code AS rep_code,
            c_o.code AS nat_code,
            c_p.Latitude AS lat1,
            c_p.Longitude AS lon1,
            c_o.Latitude AS lat2,
            c_o.Longitude AS lon2
        FROM
            athlete_codes p
            JOIN athletes a ON a.code = p.a_code
            JOIN countries c_p ON c_p.code = a.country_code
            JOIN countries c_o ON c_o.code = a.nationality_code
        WHERE
            c_p.Latitude IS NOT NULL
            AND c_p.Longitude IS NOT NULL
            AND c_o.Latitude IS NOT NULL
            AND c_o.Longitude IS NOT NULL
    )
SELECT
    name AS ATHLETE_NAME,
    rep_code AS REPRESENTED_COUNTRY_CODE,
    nat_code AS NATIONALITY_COUNTRY_CODE
FROM
    valid_athletes
ORDER BY
    (lat1 - lat2) * (lat1 - lat2) + (lon1 - lon2) * (lon1 - lon2) DESC,
    ATHLETE_NAME ASC;