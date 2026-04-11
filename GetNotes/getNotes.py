import requests

end = 21
start = 21

base_url = "http://www.eecs70.org/assets/pdf/notes/n{}.pdf"

for i in range(start,end + 1):
    url = base_url.format(i)
    filename = f"n{i}.pdf"

    try:
        print(f"Downloading {url}...")
        reponse = requests.get(url)

        if reponse.status_code == 200:
            with open(filename,"wb") as f:
                f.write(reponse.content)
            print(f"{filename} Saved!")
        else:
            print(f"{filename} GET failed")

    except Exception as e:
        print(f"Error downloading {url}: {e}")
