# How to present Vardhan (8-10 minutes)

## The story (learn this by heart, 30 seconds)

> "Most young people start investing because a friend said *'buy this stock'*. They don't read the news, and their apps only show how much they could **gain**. Nobody shows them how much they could **lose**.
>
> Vardhan fixes that. You tell it your age, how much you can invest and what you're saving for. It builds a plan for *you*, an AI reads the latest news about each company, and it shows you, in rupees, what a bad year would cost you before you invest.
>
> **Vardhan** is Sanskrit for 'growth'. It's written in C, and each feature runs on one of the data structures from our course."

---

## The demo (what to click and what to say)

### 0. Opening (20 s)
Start the app with the laptop on the projector. The logo spins in, the V draws itself and the name slides out (about 3 seconds). On the sign-in page, move the mouse: the logo leans towards it, and clicking it makes it spin.
> *"The logo is a V whose right arm becomes a rising arrow: growth, and a tick for 'checked before you invest'. It's drawn live in C, which is why it can move like this."*

Text too small on the projector? **Ctrl +** makes everything bigger (**Ctrl 0** resets).

### Safest demo: one click
On the sign-in page click **Try the demo**. It builds Aarav's account fresh every time: profile, plan, six months of investments and two alerts. Use this if you're short on time or nervous; nothing needs typing. (You can still show account creation afterwards.)

### 1. Create an account (1 min)
For a fresh demo, delete `data/users.txt` and the `data/profile_*.txt` files first. Click **Create my account**.
- **About you:** type a name, your age (e.g. 21) and a 4-digit PIN. *"Each person on the laptop has their own account. The PIN is never saved, only a salted hash, and names are looked up in our hash table."*
- **Money:** ₹1 lakh now, ₹5,000 a month.
- **Goals:** Education, ₹6 lakh, 3 years → **Add**. House, ₹20 lakh, 8 years → **Add**.
  *"Money I need in 3 years shouldn't be in risky stocks."*
- **Quiz:** answer honestly.
- **Result:** the needle moves to your profile, e.g. *Balanced*. Click **Build my plan**.

### 2. Home (1 min)
After **Build my plan** you land on Home. It shows ₹0 at first. Click **6 months ago** under "See what your plan would be worth today if you had started".
> *"This is my portfolio as if I'd started this plan six months ago, priced from real history. I invested ₹3 lakh; here's what it's worth today, my profit or loss in rupees and percent, and my XIRR, the yearly return that accounts for when each rupee went in."*
- Click **Transactions**: *"Every investment is a node in a linked list, newest first."*
- *"To price a purchase, I find the closing price on that date with binary search over the sorted trading days."*
- *"XIRR is solved with Newton's method."*
- **+ Add investment**: pick a stock, an amount and a date (today or backdated).

- **Alert banner** at the top of Home: *"I asked Vardhan to tell me if Nvidia's news turned bad. Triggered alerts go into a max-heap, so the most urgent one is always on top."* Set one live from **Explore → Alert me**.
- **Download report**: one click makes a 4-page PDF and opens it. *"I wrote the PDF writer myself: each page is drawn off-screen, compressed, and written as PDF objects with a cross-reference table."*
- **Vani bubble** (bottom-right, on every page): click it and ask "How is my portfolio doing?"

### 3. My Plan (1.5 min)
(After **Build my plan**, a screen ticks through what the engine is doing, naming the max-heap, the graph and BFS, and the 3,000 simulated years.)
- **Donut:** *"Here's where my money goes. Part of it is kept safe because one goal is only 3 years away."*
- **Fan chart:** *"The app simulated 3,000 possible years using real price history. The middle line is the most likely result. The bottom edge is a bad year, which happens about 1 year in 20."*
- **What could go wrong:** *"This is the part other apps don't show. A bad year costs me this much. If a Covid-style crash happened, this much."*
- **Goals:** *"72% chance I reach my education goal. For the house, it tells me how much more to save each month."*
- **What if...?** Click it and drag **Invest every month** up by ₹5,000. *"Every time I move a slider, it re-runs 2,000 simulated futures for each goal. My house goal just went from 50% to 80%. Students can see exactly what saving a bit more buys them."* Drag the house deadline out by a year too, then click **Reset** (or **Use these numbers**).

### 3. Friend's tip check (1.5 min): the "wow" moment
Type **nv**, pick **NVDA**, amount ₹50,000, click **Check it**.
> *"Say my friend tells me to buy Nvidia. Vardhan says it's **risky for me right now**. Here's why: the AI read the news and found that Nvidia's CEO resigned on 12 November, and the US tightened chip export rules. There's a 68% chance I'd have less money after one year. And here are three stocks that fit me better."*

Now click **Why? Read the news**. The front page spins in.
> *"Most people never read the news before buying. So Vardhan turns it into a one-page newspaper. Here's the headline, what happened, and the key part: **before the news** a bad year would cost me ₹31,000; **after the news** it's ₹39,000. The news made this stock riskier, in rupees."* Click any stock in the **In this edition** strip, or use the ← → arrow keys, to flip through every holding and every stock Vardhan avoided. The bottom row, **Red flags**, shows the stocks with the worst news right now. On My Plan, the **Avoided** tab on the news card lists them too, and clicking any holding opens its page.

### 4. Ask Vani (1 min)
Click **Explain this to me** on the Friend's tip result, or open **Ask Vani** and click a suggested question.
> *"Beginners don't want charts, they want to ask a question. The first answer appears instantly: it's computed from Vardhan's own simulations, so it works even with no AI. A moment later the on-device AI explains it in plain English."*

Then type your own question, e.g. **How much could I lose in a bad year?** or **Is Reliance good for my house?**
> *"The AI can't make numbers up. Vardhan finds the stocks in my question using the **hash table** and the **trie**, gives the AI a fact sheet with my goals, my plan and the real numbers, and tells it to use only those facts."*

### 5. Proof it works (1 min): answer "how do you know it's right?" before they ask
> *"Any app can draw a prediction. So we tested ours. Vardhan hides the last 12 months of prices, builds my plan as if it were a year ago, and predicts a range. Then it replays what really happened."* Wait for the line to finish drawing.
- If it landed inside: *"It stayed inside the predicted range for most of the year."*
- If it landed outside: *"This is the 1-in-10 case. We don't hide it, and that's why the What could go wrong section exists."*
- Point at the pink box: *"And here's the friend's tip: the stock that had shot up the year before. Look how much deeper it fell along the way."*
- Bottom chart: *"We promise a 'bad month' only happens 1 time in 20. We checked that promise over 1,000 times across 23 stocks."* Read out the number. If it's above 5%, say: *"Real markets crash more often than one year of history suggests. That's exactly why we add the AI's news risk and the Covid and 2008 crash tests on top."* Examiners love an honest answer here.

### 6. Explore (1 min)
Click a better alternative, or open **Explore stocks** and type **tata** in the search.
- *"This search uses a **trie**: 'tata' finds both Tata companies as I type."*
- Drag across the price chart: *"A **segment tree** gives the highest and lowest price for any period instantly."*
- Show the **AI news analysis**: the factors and the headlines it read.
- Type an amount in **What if I invest**: the red and green bars show losses vs gains.

### 7. Under the hood (1 min)
*"Here are all eight data structures, with live numbers."* Point at the graph:
> *"Stocks that move together are linked. This is a **graph**, and BFS finds the clusters. My plan never picks two companies from the same cluster, so one bad event can't hit everything I own."*

### 8. Settings & AI (30 s)
Open **Settings & AI**: *"The AI runs **on this laptop**, a model called Llama through Ollama. No API key, no usage limits, it works offline, and your financial data never leaves your device."*
If you have Wi-Fi: open NVDA in Explore and click **Re-read the news**. *"It fetches today's headlines and the on-device AI reads them, in a background **queue**, so the app never freezes."*
No Wi-Fi? No problem: the app shows the saved analysis with its date. Say *"it updated this morning when I opened it."*

### 9. Tests (30 s)
Run `Vardhan-Tests.exe` and show **63 passed**: *"Including edge cases: empty search, a full queue, garbage replies from the AI, and an AI that returns crazy numbers, which get clamped."*

---

## Before the exam (one-time setup at home, with internet)

1. Install **Ollama** (ollama.com), open it, and in a terminal run: `ollama pull llama3.2:3b`.
2. Open Vardhan, go to **Settings & AI → On-device AI → Save → Test connection** and wait for "AI connected".
3. Click **Download real prices**, then **Let the AI read today's news for every stock**. Leave it running; it takes a few minutes on a laptop, and the sidebar shows progress.
4. From now on Vardhan refreshes itself every time it opens. On the morning of the exam, open it once with Wi-Fi.
5. In the lab, with or without Wi-Fi, everything works: it uses what it saved.
6. Practise the demo twice. Delete `data/profile.txt` right before the demo so onboarding starts fresh.

## Viva questions (short answers)

**How do you "predict" losses? Nobody can predict prices.**
Right, so we don't predict one price. We simulate 3,000 possible years by re-shuffling the stock's real daily moves from the last two years. That gives a *range*. We report the bad end of it: the 5th percentile, and the average of the worst 5%. Banks do the same; it's called Value at Risk / Expected Shortfall.

**What exactly does the AI do?**
It reads recent headlines and returns numbers: how much jumpier the stock will be (`vol_mult`), a change in expected return (`drift_tilt`) and the chance of a sudden drop (`event_prob`, `event_size`), plus plain-English reasons. Our simulation uses those numbers. Every number is clamped to a safe range, so a wrong AI answer can't break the results.

**How do you know your predictions are any good?**
The **Proof it works** screen. We hide the last year of prices, rebuild the plan with only the old data and check the real outcome against the predicted range (a back-test). Then we check calibration: a 95% "bad month" limit should be broken about 5% of the time. We test it on a rolling one-year window, every 5 trading days, across every stock, and show the real rate, even when it's worse than 5%.

**Can the chatbot give wrong numbers?**
It's grounded: the AI only gets a fact sheet built by our engine (the user's goals, plan, simulations, news factors) and is told to use only those facts. Every question also gets an instant answer computed directly by Vardhan, shown first, so you can see the AI's numbers match. If the AI is off or slow, the instant answer is still there.

**Is the news real?**
Yes. The bundled analyses are based on real news up to 18 Nov 2026 and were checked against published reports: Jensen Huang resigned as Nvidia CEO on 12 Nov with CFO Colette Kress as interim CEO; Nvidia's Q3 revenue rose 103% to $115.8 billion; Reliance posted a record ₹29,550 crore quarterly profit; Asian Paints' profit rose only 7.5% as raw-material costs jumped; ITC is down about 30% in 2026 after a cigarette-tax hike. When the AI is on, it fetches fresh headlines and writes a new reading.

**How is the newspaper made?**
Nothing is downloaded as a page. Vardhan builds it from data it already has: the AI's factors (sorted by impact, with an insertion sort), a headline written by a template from the factor categories, the news headlines from the **linked list**, the last six months of prices, and two simulations of your money, one with the AI's news adjustment and one without. The difference between them is the news, in rupees.

**Why did the AI used to take minutes, and what did you change?**
The on-device AI runs on the laptop's CPU. At first every background task shared one queue, so a question could wait behind 23 news analyses. Now there are two queues on two threads: downloads never wait for the AI, and your question goes to the **front** of the AI queue (a deque) and interrupts any background analysis, which is re-queued for later. The answer streams in word by word, so you see it being written.

**How does sign-in work?**
Accounts are stored in `users.txt`: name, a random salt and a hash of the PIN stretched over 20,000 rounds. The plain PIN is never written down. Names are found in the hash table. Five wrong tries lock the account for 30 seconds. A real product would use bcrypt or Argon2 and a server.

**What's the spinning coin?**
Our loading indicator. Both faces are drawn once into textures, then squeezed sideways every frame by the cosine of the angle, with a darker rim for thickness. It shows whenever work runs in the background queue.

**How does the What-if update so fast?**
It reuses the plan and re-runs only the goal simulation: 2,000 random futures per goal, month by month, sampling real 21-day blocks of price history. It waits until you pause dragging for a moment so the sliders stay smooth.

**How do alerts work?**
Each alert is a stock, a type (falls below, rises above, news turns bad) and a level. Whenever prices or news update, every alert is checked; the triggered ones are pushed into a max-heap keyed by how far past their line they are, so the banner shows the root of the heap: the most urgent alert, in O(1).

**How is the PDF made?**
Each page is drawn with the same drawing code as the app into an off-screen texture (1754 × 1240 px), read back as pixels, compressed with DEFLATE and wrapped as a zlib stream, then written as a PDF image object. The file has a catalog, a page tree, one object per page and a cross-reference table of byte offsets: all written by hand.

**Where do the sounds come from?**
They're generated at start-up: the click is a short decaying 1.9 kHz sine wave; the ka-ching is two bell notes (C7 and G7) a few milliseconds apart with a burst of noise.

**How is "how much I invested" tracked?**
Each investment (date, stock, amount, price that day) is a transaction in a linked list saved to the user's own file. Value today = amount × today's price ÷ the price on the purchase day, found by binary search on dates (O(log n)). Deposits grow at 7% a year. XIRR finds the yearly rate r where all cash flows balance, using Newton's method.

**Why a heap for picking stocks?**
We score every stock for the user and want the best one, then the next best, and so on. A max-heap gives the top in O(1) and removes it in O(log n). We stop as soon as the plan is full, so we never sort everything.

**Why a graph?**
To diversify. If two stocks' daily moves have a correlation above 0.6 they're linked. BFS finds connected groups (clusters), and the advisor won't put two companies from the same cluster in your plan.

**Why a trie for search?**
Search-as-you-type needs prefix lookups. A trie finds all words starting with what you typed in time proportional to the length of the query, however many stocks there are. Company-name words are stored too, so "bank" finds HDFC Bank and ICICI Bank.

**Why a segment tree?**
Dragging across a chart asks "highest and lowest price between day A and day B". The segment tree answers in O(log n) instead of scanning every day.

**Why a queue and a worker thread?**
Downloading prices, news and AI answers takes seconds each. If the UI did it, the app would freeze. Jobs go into a queue; a background worker processes them in order (FIFO) and the UI picks up the results.

**How does the risk profile work?**
55% quiz answers, 25% age, 20% how soon your nearest goal is. A 22-year-old saving for 8 years gets more stocks than someone who needs the money next year, even with the same answers.

**Why run the AI on the laptop instead of the cloud?**
Three reasons: privacy (your money and goals never leave your device), cost (no API bills or usage limits), and reliability (it works without internet). For a finance app, privacy matters a lot. If the local AI isn't running, the app can fall back to Gemini, and if nothing is reachable it shows the last saved analysis.

**Why does the plan keep money in a fixed deposit?**
Money needed soon shouldn't be in stocks: a crash right before you need it can't be undone. The safe share grows as goals get closer.

**Is this financial advice?**
No. It's an education tool. Personalised investment advice in India needs SEBI registration. The app says so on every relevant screen.

**What would you add?**
Mutual funds, taxes, connecting a real broker account, and fat-tailed crash simulations so the calibration on the Proof screen gets closer to 5%.
